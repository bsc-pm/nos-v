/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef DTLOCK_H
#define DTLOCK_H

#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "common.h"
#include "compiler.h"
#include "defaults.h"
#include "generic/arch.h"
#include "generic/bitset.h"
#include "generic/futex.h"
#include "memory/slab.h"
#include "scheduler/cpubitset.h"
#include "scheduler/governor.h"

typedef atomic_uint_fast64_t atomic_uint64_t;

struct dtlock_node {
	atomic_uint64_t ticket __cacheline_aligned;
	atomic_uint64_t cpu;
};

union dtlock_signal {
	atomic_uint val;
	unsigned int raw_val;
	struct {
		unsigned char signal_flags : 1;
		int signal_cnt : 31;
	};
};

struct dtlock_item {
	uint64_t ticket __cacheline_aligned;
	void *item;
	union dtlock_signal signal;
	unsigned int next;
	unsigned char flags;
};

#define DTLOCK_SIGNAL_DEFAULT 0x0
#define DTLOCK_SIGNAL_SLEEP 0x1
#define DTLOCK_SIGNAL_WAKE  0x2

#define ITEM_DTLOCK_EAGAIN ((void *) 1)

#define DTLOCK_EAGAIN 2
#define DTLOCK_SERVER 1
#define DTLOCK_SERVED 0

#define DTLOCK_FLAGS_NONE 0x0
#define DTLOCK_FLAGS_NONBLOCK 0x1

typedef struct delegation_lock {
	atomic_uint64_t head __cacheline_aligned;
	uint64_t size __cacheline_aligned;
	uint64_t next __cacheline_aligned;
	struct dtlock_node *waitqueue;
	struct dtlock_item *items;
	nosv_futex_t *cpu_sleep_vars;
} delegation_lock_t;

static inline void dtlock_init(delegation_lock_t *dtlock, int size)
{
	assert(dtlock);

	dtlock->size = size;
	dtlock->next = size + 1;
	atomic_init(&dtlock->head, size);
	dtlock->waitqueue = (struct dtlock_node *)salloc(sizeof(struct dtlock_node) * size, -1);
	dtlock->items = (struct dtlock_item *)salloc(sizeof(struct dtlock_item) * size, -1);
	dtlock->cpu_sleep_vars = (nosv_futex_t *)salloc(sizeof(nosv_futex_t) * size, -1);

	for (int i = 0; i < size; ++i) {
		atomic_init(&dtlock->waitqueue[i].cpu, 0);
		atomic_init(&dtlock->waitqueue[i].ticket, 0);
		atomic_init(&dtlock->items[i].signal.val, 0);
		dtlock->items[i].ticket = 0;
		dtlock->items[i].item = 0;
		dtlock->items[i].flags = DTLOCK_FLAGS_NONE;

		nosv_futex_init(&dtlock->cpu_sleep_vars[i]);
	}

	atomic_store_explicit(&dtlock->waitqueue[0].ticket, size, memory_order_seq_cst);
}

static inline int dtlock_try_lock(delegation_lock_t *dtlock)
{
	uint64_t head = atomic_load_explicit(&dtlock->head, memory_order_relaxed);
	const uint64_t id = head % dtlock->size;

	if (atomic_load_explicit(&dtlock->waitqueue[id].ticket, memory_order_relaxed) != head)
		return 0;

	int res = atomic_compare_exchange_weak_explicit(&dtlock->head, &head, head + 1, memory_order_acquire, memory_order_relaxed);

	return res;
}

static inline void dtlock_lock(delegation_lock_t *dtlock)
{
	const uint64_t head = atomic_fetch_add_explicit(&dtlock->head, 1, memory_order_relaxed);
	const uint64_t id = head % dtlock->size;

	// Wait until its our turn
	while (atomic_load_explicit(&dtlock->waitqueue[id].ticket, memory_order_relaxed) != head)
		spin_wait();
	spin_wait_release();

	atomic_thread_fence(memory_order_acquire);
}

static inline int dtlock_lock_or_delegate(delegation_lock_t *dtlock, const uint64_t cpu_index, void **item, const int blocking)
{
	union dtlock_signal prev_signal, signal;
	prev_signal.raw_val = atomic_load_explicit(&dtlock->items[cpu_index].signal.val, memory_order_acquire);

	const uint64_t head = atomic_fetch_add_explicit(&dtlock->head, 1, memory_order_relaxed);
	const uint64_t id = head % dtlock->size;

	assert(cpu_index < dtlock->size);
	assert(id < dtlock->size);

	dtlock->items[cpu_index].flags = blocking ? DTLOCK_FLAGS_NONBLOCK : DTLOCK_FLAGS_NONE;
	atomic_store_explicit(&dtlock->waitqueue[id].cpu, head + cpu_index, memory_order_release);

	while (atomic_load_explicit(&dtlock->waitqueue[id].ticket, memory_order_relaxed) < head)
		spin_wait();
	spin_wait_release();

	// Lock acquired
	if (dtlock->items[cpu_index].ticket != head) {
		atomic_thread_fence(memory_order_acquire);
		return DTLOCK_SERVER;
	}

	while ((signal.raw_val = atomic_load_explicit(&dtlock->items[cpu_index].signal.val, memory_order_relaxed), signal.signal_cnt) == prev_signal.signal_cnt)
		spin_wait();
	spin_wait_release();

	// Delegated and served
	// We may be asked to sleep
	if (unlikely(signal.signal_flags & DTLOCK_SIGNAL_SLEEP)) {
		assert(blocking);
		nosv_futex_wait(&dtlock->cpu_sleep_vars[cpu_index]);
	} else {
		atomic_thread_fence(memory_order_acquire);
	}

	void *recv_item = dtlock->items[cpu_index].item;

	if(recv_item == ITEM_DTLOCK_EAGAIN) {
		// It might happen that we raced against the thread serving
		return DTLOCK_EAGAIN;
	} else {
		*item = recv_item;
		return DTLOCK_SERVED;
	}
}

// Must be called with lock acquired
// Pops the front element of the dtlock and transfers it to the second wait queue
static inline void dtlock_popfront_wait(delegation_lock_t *dtlock, uint64_t cpu)
{
	const uint64_t id = dtlock->next % dtlock->size;
	assert(id < dtlock->size);

	dtlock->items[cpu].ticket = dtlock->next;
	atomic_store_explicit(&dtlock->waitqueue[id].ticket, dtlock->next++, memory_order_release);
}

// Must be called with lock acquired
static inline void dtlock_popfront(delegation_lock_t *dtlock)
{
	const uint64_t id = dtlock->next % dtlock->size;
	assert(id < dtlock->size);
	atomic_store_explicit(&dtlock->waitqueue[id].ticket, dtlock->next++, memory_order_release);
}

// Must be called with lock acquired
static inline int dtlock_empty(const delegation_lock_t *dtlock)
{
	const uint64_t cpu = atomic_load_explicit(&dtlock->waitqueue[dtlock->next % dtlock->size].cpu, memory_order_relaxed);
	return (cpu < dtlock->next);
}

// Must be called with lock acquired
static inline uint64_t dtlock_front(const delegation_lock_t *dtlock)
{
	const uint64_t cpu = atomic_load_explicit(&dtlock->waitqueue[dtlock->next % dtlock->size].cpu, memory_order_relaxed);
	return cpu - dtlock->next;
}

// Must be called with lock acquired
static inline void dtlock_serve(delegation_lock_t *dtlock, const uint64_t cpu, void *item, int signal)
{
	assert(cpu < dtlock->size);
	dtlock->items[cpu].item = item;

	if (signal == DTLOCK_SIGNAL_WAKE) {
		nosv_futex_signal(&dtlock->cpu_sleep_vars[cpu]);
	} else {
		union dtlock_signal sig;
		sig.signal_cnt = ++dtlock->items[cpu].next;
		sig.signal_flags = signal;

		// Unlock the thread
		atomic_store_explicit(&dtlock->items[cpu].signal.val, sig.raw_val, memory_order_release);
	}
}

// Must be called with lock acquired
static inline int dtlock_get_waiters(delegation_lock_t *dtlock, cpu_bitset_t *bitset)
{
	int waiters = cpu_bitset_count(bitset);

	while (!dtlock_empty(dtlock)) {
		uint64_t cpu = dtlock_front(dtlock);
		assert(!cpu_bitset_isset(bitset, cpu));
		cpu_bitset_set(bitset, (int) cpu);
		dtlock_popfront_wait(dtlock, cpu);
		waiters++;
	}

	return waiters;
}

// Must be called with lock acquired
static inline void dtlock_unlock(delegation_lock_t *dtlock)
{
	dtlock_popfront(dtlock);
}

// Must be called with lock acquired
static inline int dtlock_blocking(const delegation_lock_t *dtlock, const int cpu)
{
	return !(dtlock->items[cpu].flags & DTLOCK_FLAGS_NONBLOCK);
}

#endif // DTLOCK_H
