/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2025 Barcelona Supercomputing Center (BSC)
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
#include "instr.h"
#include "generic/arch.h"
#include "generic/bitset.h"
#include "generic/futex.h"
#include "memory/slab.h"
#include "scheduler/cpubitset.h"

#define DTLOCK_SIGNAL_DEFAULT 0x0
#define DTLOCK_SIGNAL_SLEEP 0x1
#define DTLOCK_SIGNAL_WAKE 0x2

#define DTLOCK_ITEM_RETRY ((void *) 0x1)

#define DTLOCK_FLAGS_NONE 		0
#define DTLOCK_FLAGS_NONBLOCK 	1
#define DTLOCK_FLAGS_EXTERNAL 	2

// Union to decode the bits forming the dtlock_node.cpu field
union dtlock_node_cpu {
	uint64_t raw_val;
	struct {
		unsigned char flags : 2;
		uint64_t cpu : 62;
	};
};

struct dtlock_node {
	atomic_uint64_t ticket __cacheline_aligned;
	atomic_uint64_t cpu; // Accessed through bitfield union dtlock_node_cpu
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
	uint32_t scheduled_count;
	atomic_uint32_t signal; // Accessed through union dtlock_signal
	uint32_t next;
	unsigned char flags;
};

typedef struct delegation_lock {
	// The three constant fields in the same cacheline
	struct dtlock_node *waitqueue __cacheline_aligned;
	struct dtlock_item *items;
	uint64_t size;
	nosv_futex_t *cpu_sleep_vars;

	atomic_uint64_t head __2xcacheline_aligned;
	uint64_t next __2xcacheline_aligned;

} delegation_lock_t;

#define DTLOCK_SPIN(a, b, cmp, emit_resting)                                \
	{                                                                       \
		int __spins = 0;                                                    \
		typeof((a)) __a = atomic_load_explicit(&(a), memory_order_relaxed); \
		while (__a cmp(b)                                                   \
			   && __spins++ < IDLE_SPINS_THRESHOLD) {                       \
			spin_wait();                                                    \
			__a = atomic_load_explicit(&(a), memory_order_relaxed);         \
		}                                                                   \
		spin_wait_release();                                                \
		if (__a cmp(b)) {                                                   \
			if (emit_resting)                                               \
				instr_worker_resting();                                     \
			__a = atomic_load_explicit(&(a), memory_order_relaxed);         \
			while (__a cmp(b)) {                                            \
				spin_wait();                                                \
				__a = atomic_load_explicit(&(a), memory_order_relaxed);     \
			}                                                               \
			spin_wait_release();                                            \
		}                                                                   \
	}

#define DTLOCK_SPIN_EQ(a, b, r) DTLOCK_SPIN((a), (b), !=, (r))
#define DTLOCK_SPIN_LT(a, b, r) DTLOCK_SPIN((a), (b), <, (r))

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

static inline void dtlock_free(delegation_lock_t *dtlock, int size)
{
	assert(dtlock);

	for (int i = 0; i < size; ++i)
		nosv_futex_destroy(&dtlock->cpu_sleep_vars[i]);

	sfree(dtlock->cpu_sleep_vars, sizeof(nosv_futex_t) * size, -1);
	sfree(dtlock->items, sizeof(struct dtlock_item) * size, -1);
	sfree(dtlock->waitqueue, sizeof(nosv_futex_t) * size, -1);
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
	DTLOCK_SPIN_EQ(dtlock->waitqueue[id].ticket, head, 1);

	atomic_thread_fence(memory_order_acquire);
}

static inline int dtlock_lock_or_delegate(
	delegation_lock_t *dtlock,
	const uint64_t cpu_index,
	void **item,
	uint32_t *scheduled_count,
	const int blocking,
	const int external)
{
	union dtlock_signal prev_signal, signal;
	void *recv_item;
	uint32_t recv_count;

	assert(dtlock);
	assert(cpu_index < dtlock->size);

	do {
		prev_signal.raw_val = atomic_load_explicit(&dtlock->items[cpu_index].signal, memory_order_relaxed);

		const uint64_t head = atomic_fetch_add_explicit(&dtlock->head, 1, memory_order_relaxed);
		const uint64_t id = head % dtlock->size;

		assert(id < dtlock->size);

		// Register into the first waitqueue, which the server should empty as soon as possible
		union dtlock_node_cpu wait_cpu;
		wait_cpu.cpu = head + cpu_index;
		wait_cpu.flags = blocking ? DTLOCK_FLAGS_NONE : DTLOCK_FLAGS_NONBLOCK;
		wait_cpu.flags |= external ? DTLOCK_FLAGS_NONE : DTLOCK_FLAGS_EXTERNAL;
		atomic_store_explicit(&dtlock->waitqueue[id].cpu, wait_cpu.raw_val, memory_order_relaxed);

		DTLOCK_SPIN_LT(dtlock->waitqueue[id].ticket, head, blocking);

		atomic_thread_fence(memory_order_acquire);

		// At this point, either we have acquired the lock because there was no-one waiting,
		// or we have been moved to the second waiting location
		if (dtlock->items[cpu_index].ticket != head) {
			// Lock acquired
			dtlock->items[cpu_index].flags = wait_cpu.flags;
			return 1;
		}

		// We have to wait again
		// Here we spin on the items[]->signal variable, where we can be notified of two things:
		// Either we are served an item, or we have to go to sleep
		// We may miss a signal in between, that's why we use the signal_cnt instead of a single bit
		signal.raw_val = atomic_load_explicit(&dtlock->items[cpu_index].signal, memory_order_relaxed);

		if (signal.signal_cnt == prev_signal.signal_cnt) {
			if (blocking)
				instr_worker_resting();
			while (signal.signal_cnt == prev_signal.signal_cnt) {
				spin_wait();
				signal.raw_val = atomic_load_explicit(&dtlock->items[cpu_index].signal, memory_order_relaxed);
			}
			spin_wait_release();
		}

		// Check if we have been asked to sleep or not
		if (unlikely(signal.signal_flags & DTLOCK_SIGNAL_SLEEP)) {
			// Must be blocking, so emit resting unconditionally
			instr_worker_resting();
			assert(blocking);
			nosv_futex_wait(&dtlock->cpu_sleep_vars[cpu_index]);
		} else {
			atomic_thread_fence(memory_order_acquire);
		}

		recv_item = dtlock->items[cpu_index].item;
		recv_count = dtlock->items[cpu_index].scheduled_count;

	} while (recv_item == DTLOCK_ITEM_RETRY);

	*item = recv_item;
	*scheduled_count = recv_count;
	// Served
	return 0;
}

// Must be called with lock acquired
// Pops the front element of the dtlock and transfers it to the second wait queue
static inline void dtlock_popfront_wait(delegation_lock_t *dtlock, uint64_t cpu)
{
	const uint64_t id = dtlock->next % dtlock->size;
	assert(cpu < dtlock->size);

	// Transfer flags from waitqueue to items
	union dtlock_node_cpu wait_cpu;
	wait_cpu.raw_val = atomic_load_explicit(&dtlock->waitqueue[id].cpu, memory_order_relaxed);
	dtlock->items[cpu].flags = wait_cpu.flags;

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
	union dtlock_node_cpu wait_cpu;
	wait_cpu.raw_val = atomic_load_explicit(&dtlock->waitqueue[dtlock->next % dtlock->size].cpu, memory_order_relaxed);

	return (wait_cpu.cpu < dtlock->next);
}

// Must be called with lock acquired
static inline uint64_t dtlock_front(const delegation_lock_t *dtlock)
{
	union dtlock_node_cpu wait_cpu;
	wait_cpu.raw_val = atomic_load_explicit(&dtlock->waitqueue[dtlock->next % dtlock->size].cpu, memory_order_relaxed);

	return wait_cpu.cpu - dtlock->next;
}

// Must be called with lock acquired
static inline void dtlock_serve(delegation_lock_t *dtlock, const uint64_t cpu, void *item, uint32_t scheduled_count, int signal)
{
	assert(cpu < dtlock->size);
	dtlock->items[cpu].item = item;
	dtlock->items[cpu].scheduled_count = scheduled_count;

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

// Must be called with lock acquired
static inline int dtlock_requires_external(const delegation_lock_t *dtlock, const int cpu)
{
	assert(cpu < dtlock->size);
	return !(dtlock->items[cpu].flags & DTLOCK_FLAGS_EXTERNAL);
}

#endif // DTLOCK_H
