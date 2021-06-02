/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef DTLOCK_H
#define DTLOCK_H

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>

#include "compiler.h"
#include "generic/arch.h"
#include "memory/slab.h"

typedef atomic_uint_fast64_t atomic_uint64_t;

struct dtlock_node {
	atomic_uint64_t ticket __cacheline_aligned;
	atomic_uint64_t cpu;
};

struct dtlock_item {
	uint64_t ticket __cacheline_aligned;
	void *item;
};

typedef struct delegation_lock {
	atomic_uint64_t head __cacheline_aligned;
	uint64_t size __cacheline_aligned;
	uint64_t next __cacheline_aligned;
	struct dtlock_node *waitqueue;
	struct dtlock_item *items;
} delegation_lock_t;

static inline void dtlock_init(delegation_lock_t *dtlock, int size)
{
	assert(dtlock);

	dtlock->size = size;
	dtlock->next = size + 1;
	atomic_init(&dtlock->head, size);
	dtlock->waitqueue = (struct dtlock_node *)salloc(sizeof(struct dtlock_node) * size, -1);
	dtlock->items = (struct dtlock_item *)salloc(sizeof(struct dtlock_item) * size, -1);

	for (int i = 0; i < size; ++i) {
		atomic_init(&dtlock->waitqueue[i].cpu, 0);
		atomic_init(&dtlock->waitqueue[i].ticket, 0);
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
	const uint64_t head = atomic_fetch_add_explicit(&dtlock->head, 1, memory_order_relaxed);
	const uint64_t id = head % dtlock->size;

	assert(cpu_index < dtlock->size);
	atomic_store_explicit(&dtlock->waitqueue[id].cpu, head + cpu_index, memory_order_relaxed);

	while (atomic_load_explicit(&dtlock->waitqueue[id].ticket, memory_order_relaxed) < head)
		spin_wait();
	spin_wait_release();

	// Guarantee the write to ticket isn't seen out of order with the items
	atomic_thread_fence(memory_order_acquire);

	// Lock acquired
	if (dtlock->items[id].ticket != head)
		return 1;

	// Delegated and served
	*item = dtlock->items[id].item;

	return 0;
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

// Must be called with lock acquired
static inline void dtlock_popfront(delegation_lock_t *dtlock)
{
	const uint64_t id = dtlock->next % dtlock->size;
	atomic_store_explicit(&dtlock->waitqueue[id].ticket, dtlock->next++, memory_order_release);
}

// Must be called with lock acquired
static inline void dtlock_unlock(delegation_lock_t *dtlock)
{
	dtlock_popfront(dtlock);
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
	dtlock->items[cpu].item = item;
	dtlock->items[cpu].ticket = dtlock->next;
}

#endif // DTLOCK_H