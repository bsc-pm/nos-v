/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef DTLOCK_H
#define DTLOCK_H

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>

#include "common.h"
#include "compiler.h"
#include "defaults.h"
#include "generic/arch.h"
#include "generic/bitset.h"
#include "memory/slab.h"

typedef atomic_uint_fast64_t atomic_uint64_t;

struct dtlock_node {
	atomic_uint64_t ticket __cacheline_aligned;
	atomic_uint64_t cpu;
};

struct dtlock_item {
	atomic_char signal __cacheline_aligned;
	uint64_t ticket;
	void *item;
};

BITSET_DEFINE(dtlock_mask, NR_CPUS)
typedef struct dtlock_mask dtlock_mask_t;

#define ITEM_DTLOCK_EAGAIN ((void *) 1)
#define DTLOCK_EAGAIN 2
#define DTLOCK_SERVER 1
#define DTLOCK_SERVED 0

typedef struct delegation_lock {
	atomic_uint64_t head __cacheline_aligned;
	uint64_t size __cacheline_aligned;
	uint64_t next __cacheline_aligned;
	struct dtlock_node *waitqueue;
	struct dtlock_item *items;
	dtlock_mask_t mask;
} delegation_lock_t;

static inline void dtlock_init(delegation_lock_t *dtlock, int size)
{
	assert(dtlock);

	dtlock->size = size;
	dtlock->next = size + 1;
	atomic_init(&dtlock->head, size);
	dtlock->waitqueue = (struct dtlock_node *)salloc(sizeof(struct dtlock_node) * size, -1);
	dtlock->items = (struct dtlock_item *)salloc(sizeof(struct dtlock_item) * size, -1);

	// Allocate mask
	BIT_ZERO(size, &dtlock->mask);

	for (int i = 0; i < size; ++i) {
		atomic_init(&dtlock->waitqueue[i].cpu, 0);
		atomic_init(&dtlock->waitqueue[i].ticket, 0);
		atomic_init(&dtlock->items[i].signal, 0);
		dtlock->items[i].ticket = 0;
		dtlock->items[i].item = 0;
	}

	atomic_store_explicit(&dtlock->waitqueue[0].ticket, size, memory_order_seq_cst);
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

static inline int dtlock_lock_or_delegate(delegation_lock_t *dtlock, const uint64_t cpu_index, void **item)
{
	const char signal = atomic_load_explicit(&dtlock->items[cpu_index].signal, memory_order_acquire);
	const uint64_t head = atomic_fetch_add_explicit(&dtlock->head, 1, memory_order_relaxed);
	const uint64_t id = head % dtlock->size;

	assert(cpu_index < dtlock->size);
	assert(id < dtlock->size);
	atomic_store_explicit(&dtlock->waitqueue[id].cpu, head + cpu_index, memory_order_relaxed);

	while (atomic_load_explicit(&dtlock->waitqueue[id].ticket, memory_order_relaxed) < head)
		spin_wait();
	spin_wait_release();

	atomic_thread_fence(memory_order_acquire);

	// Lock acquired
	if (dtlock->items[cpu_index].ticket != head)
		return DTLOCK_SERVER;

	while (atomic_load_explicit(&dtlock->items[cpu_index].signal, memory_order_relaxed) == signal)
		spin_wait();
	spin_wait_release();

	// Guarantee the write to ticket isn't seen out of order with the items
	atomic_thread_fence(memory_order_acquire);

	// Delegated and served
	// It might happen that we raced against the thread serving
	if (dtlock->items[cpu_index].item == ITEM_DTLOCK_EAGAIN)
		return DTLOCK_EAGAIN;

	*item = dtlock->items[cpu_index].item;

	return DTLOCK_SERVED;
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
static inline void dtlock_set_item(delegation_lock_t *dtlock, const uint64_t cpu, void *item)
{
	assert(cpu < dtlock->size);
	dtlock->items[cpu].item = item;

	// Remove the cpu from the waiter list
	BIT_CLR(dtlock->size, cpu, &dtlock->mask);

	// Unlock the thread
	atomic_fetch_add_explicit(&dtlock->items[cpu].signal, 1, memory_order_release);
}

// Must be called with lock acquired
static inline int dtlock_get_waiters(delegation_lock_t *dtlock)
{
	int waiters = BIT_COUNT(dtlock->size, &dtlock->mask);

	while (!dtlock_empty(dtlock)) {
		uint64_t cpu = dtlock_front(dtlock);
		assert(!BIT_ISSET(dtlock->size, cpu, &dtlock->mask));
		BIT_SET(dtlock->size, cpu, &dtlock->mask);
		dtlock_popfront_wait(dtlock, cpu);
		waiters++;
	}

	return waiters;
}

// Must be called with lock acquired
// Return the 1-index of the first cpu waiting
static inline int dtlock_get_next_waiter(delegation_lock_t *dtlock, int start)
{
	int waiter = BIT_FFS_AT(dtlock->size, &dtlock->mask, start);
	return waiter;
}

// Must be called with lock acquired
static inline void dtlock_unlock(delegation_lock_t *dtlock)
{
	// Check if the DTLock is empty currently
	if (dtlock_empty(dtlock)) {
		// If it is, check if we have any pending waiter in the secondary
		// per-cpu waitqueue
		int waiter = dtlock_get_next_waiter(dtlock, 0);

		// If there is, unlock it so it can go back into the scheduler
		// Do so with a special "try again" value
		if (waiter) {
			dtlock_set_item(dtlock, waiter - 1, ITEM_DTLOCK_EAGAIN);
		}
	}
	dtlock_popfront(dtlock);
}

#endif // DTLOCK_H
