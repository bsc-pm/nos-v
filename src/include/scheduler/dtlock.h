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

#define DTLOCK_SIGNAL_DEFAULT 0x0
#define DTLOCK_SIGNAL_SLEEP 0x1
#define DTLOCK_SIGNAL_WAKE 0x2

#define DTLOCK_ITEM_RETRY ((void *) 0x1)

#define DTLOCK_FLAGS_NONE 0x0
#define DTLOCK_FLAGS_NONBLOCK 0x1

struct dtlock_node {
	atomic_uint64_t ticket __cacheline_aligned;
	atomic_uint64_t cpu;
};

// Union to decode the bits forming the dtlock_item.signal field
union dtlock_signal {
	uint32_t raw_val;
	struct {
		unsigned char signal_flags : 1;
		uint32_t signal_cnt : 31;
	};
};

struct dtlock_item {
	uint64_t ticket __cacheline_aligned;
	void *item;
	atomic_uint32_t signal;
	uint32_t next;
	unsigned char flags;
};

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
	dtlock->waitqueue = (struct dtlock_node *) salloc(sizeof(struct dtlock_node) * size, -1);
	dtlock->items = (struct dtlock_item *) salloc(sizeof(struct dtlock_item) * size, -1);
	dtlock->cpu_sleep_vars = (nosv_futex_t *) salloc(sizeof(nosv_futex_t) * size, -1);

	for (int i = 0; i < size; ++i) {
		atomic_init(&dtlock->waitqueue[i].cpu, 0);
		atomic_init(&dtlock->waitqueue[i].ticket, 0);
		atomic_init(&dtlock->items[i].signal, 0);
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
	void *recv_item;

	do {
		prev_signal.raw_val = atomic_load_explicit(&dtlock->items[cpu_index].signal, memory_order_relaxed);

		const uint64_t head = atomic_fetch_add_explicit(&dtlock->head, 1, memory_order_relaxed);
		const uint64_t id = head % dtlock->size;

		assert(cpu_index < dtlock->size);
		assert(id < dtlock->size);

		// Register into the first waitqueue, which the server should empty as soon as possible
		dtlock->items[cpu_index].flags = blocking ? DTLOCK_FLAGS_NONBLOCK : DTLOCK_FLAGS_NONE;
		atomic_store_explicit(&dtlock->waitqueue[id].cpu, head + cpu_index, memory_order_release);

		while (atomic_load_explicit(&dtlock->waitqueue[id].ticket, memory_order_relaxed) < head)
			spin_wait();
		spin_wait_release();

		atomic_thread_fence(memory_order_acquire);

		// At this point, either we have acquired the lock because there was no-one waiting,
		// or we have been moved to the second waiting location
		if (dtlock->items[cpu_index].ticket != head) {
			// Lock acquired
			return 1;
		}

		// We have to wait again
		// Here we spin on the items[]->signal variable, where we can be notified of two things:
		// Either we are served an item, or we have to go to sleep
		// We may miss a signal in between, that's why we use the signal_cnt instead of a single bit
		signal.raw_val = atomic_load_explicit(&dtlock->items[cpu_index].signal, memory_order_relaxed);
		while (signal.signal_cnt == prev_signal.signal_cnt) {
			spin_wait();
			signal.raw_val = atomic_load_explicit(&dtlock->items[cpu_index].signal, memory_order_relaxed);
		}

		spin_wait_release();

		// Check if we have been asked to sleep or not
		if (unlikely(signal.signal_flags & DTLOCK_SIGNAL_SLEEP)) {
			assert(blocking);
			nosv_futex_wait(&dtlock->cpu_sleep_vars[cpu_index]);
		} else {
			atomic_thread_fence(memory_order_acquire);
		}

		recv_item = dtlock->items[cpu_index].item;

	} while (recv_item == DTLOCK_ITEM_RETRY);

	*item = recv_item;
	// Served
	return 0;
}

// Must be called with lock acquired
// Pops the front element of the dtlock and transfers it to the second wait queue
static inline void dtlock_popfront_wait(delegation_lock_t *dtlock, uint64_t cpu)
{
	const uint64_t id = dtlock->next % dtlock->size;
	assert(cpu < dtlock->size);

	dtlock->items[cpu].ticket = dtlock->next;
	atomic_store_explicit(&dtlock->waitqueue[id].ticket, dtlock->next++, memory_order_release);
}

// Must be called with lock acquired
static inline void dtlock_popfront(delegation_lock_t *dtlock)
{
	const uint64_t id = dtlock->next % dtlock->size;
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
		atomic_store_explicit(&dtlock->items[cpu].signal, sig.raw_val, memory_order_release);
	}
}

// Must be called with lock acquired
static inline int dtlock_update_waiters(delegation_lock_t *dtlock, cpu_bitset_t *bitset)
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
static inline int dtlock_is_cpu_blockable(const delegation_lock_t *dtlock, const int cpu)
{
	assert(cpu < dtlock->size);
	return !(dtlock->items[cpu].flags & DTLOCK_FLAGS_NONBLOCK);
}

#endif // DTLOCK_H
