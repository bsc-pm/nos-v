/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef MPSC_H
#define MPSC_H

#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "compiler.h"
#include "memory/slab.h"

/*
	This is a queue that can accept multiple producers and a single consumer.
	It is not strictly lock-free, but should give acceptable performance, specially against the SPSC queue with a lock holding the producer side.
*/

typedef atomic_uint_fast64_t atomic_uint64_t;

static_assert(sizeof(atomic_uintptr_t) == sizeof(void *));

struct mpsc_queue_entry {
	atomic_uintptr_t entry;
};

typedef struct mpsc_queue {
	size_t size;
	atomic_uint64_t head __cacheline_aligned;
	atomic_uint64_t tail __cacheline_aligned;
	atomic_uint64_t count __cacheline_aligned;
	struct mpsc_queue_entry entries[] __cacheline_aligned;
} mpsc_queue_t;

static inline mpsc_queue_t *mpsc_alloc(size_t size)
{
	mpsc_queue_t *queue = (mpsc_queue_t *)salloc(sizeof(mpsc_queue_t) + size * sizeof(struct mpsc_queue_entry), -1);
	assert(queue);

	queue->size = size;
	atomic_init(&queue->head, 0);
	atomic_init(&queue->count, 0);
	atomic_init(&queue->tail, 0);

	return queue;
}

static inline int mpsc_push(mpsc_queue_t *queue, void *value)
{
	assert(value);
	const size_t size = queue->size;

	if (atomic_fetch_add_explicit(&queue->count, 1, memory_order_relaxed) >= size) {
		atomic_fetch_sub_explicit(&queue->count, 1, memory_order_relaxed);
		return 0;
	}

	// This "has" to be an acquire just for the assert?
	const uint64_t elem = atomic_fetch_add_explicit(&queue->head, 1, memory_order_acquire) % size;
	assert(queue->entries[elem].entry == 0);
	atomic_store_explicit(&queue->entries[elem].entry, (uintptr_t) value, memory_order_release);

	return 1;
}

static inline int mpsc_pop(mpsc_queue_t *queue, void **value)
{
	const size_t size = queue->size;

	const uint64_t count = atomic_load_explicit(&queue->count, memory_order_relaxed);
	if (!count)
		return 0;

	const uint64_t tail = atomic_load_explicit(&queue->tail, memory_order_acquire);
	const uint64_t next = (tail + 1) % size; // TODO: Maybe ensure this is a Po2 and use a mask.

	uintptr_t v;
	do {
		v = atomic_load_explicit(&queue->entries[tail].entry, memory_order_relaxed);
	} while (!v);

	*value = (void *)v;
	atomic_store_explicit(&queue->entries[tail].entry, 0, memory_order_relaxed);
	atomic_store_explicit(&queue->tail, next, memory_order_relaxed);
	atomic_fetch_sub_explicit(&queue->count, 1, memory_order_release);

	return 1;
}

#endif // mpsc_H