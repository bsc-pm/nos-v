/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef SPSC_H
#define SPSC_H

#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "compiler.h"
#include "memory/slab.h"

/*
	Extremely simple single-producer single-consumer queue based on a fixed-size circular buffer.
	This is similar to boost's spsc_queue, and is the basis for the nOS-V scheduler, peding future optimizations

	Improvements possible:
		- Have push_many variants, which only increment once the counters.
		- Have pop_many variants that do the same
*/

struct spsc_queue_entry {
	void *entry;
};

typedef struct spsc_queue {
	size_t size;
	atomic_uint64_t head __cacheline_aligned;
	atomic_uint64_t tail __cacheline_aligned;
	struct spsc_queue_entry entries[] __cacheline_aligned;
} spsc_queue_t;

static inline spsc_queue_t *spsc_alloc(size_t size)
{
	spsc_queue_t *queue = (spsc_queue_t *)salloc(sizeof(spsc_queue_t) + size * sizeof(struct spsc_queue_entry), -1);
	assert(queue);

	queue->size = size;
	atomic_init(&queue->head, 0);
	atomic_init(&queue->tail, 0);
	queue->tail = 0;

	return queue;
}

static inline int spsc_push(spsc_queue_t *queue, void *value)
{
	const size_t size = queue->size;
	const uint64_t head = atomic_load_explicit(&queue->head, memory_order_relaxed);
	const uint64_t next = (head + 1) % size; // TODO: Maybe ensure this is a Po2 and use a mask.

	const uint64_t tail = atomic_load_explicit(&queue->tail, memory_order_acquire);
	if (next == tail)
		return 0;

	queue->entries[head].entry = value;
	atomic_store_explicit(&queue->head, next, memory_order_release);

	return 1;
}

static inline int spsc_pop(spsc_queue_t *queue, void **value)
{
	const size_t size = queue->size;
	const uint64_t head = atomic_load_explicit(&queue->head, memory_order_acquire);
	const uint64_t tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);

	// Empty
	if (head == tail)
		return 0;

	*value = queue->entries[tail].entry;
	const uint64_t next = (tail + 1) % size; // TODO: Maybe ensure this is a Po2 and use a mask.
	atomic_store_explicit(&queue->tail, next, memory_order_release);

	return 1;
}

static inline size_t spsc_pop_batch(spsc_queue_t *queue, void **value, size_t cnt)
{
	const size_t size = queue->size;
	const uint64_t head = atomic_load_explicit(&queue->head, memory_order_acquire);
	uint64_t tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);

	// Empty
	if (head == tail)
		return 0;

	size_t remaining = (head > tail) ? (head - tail) : (size - tail + head);
	assert((tail + remaining) % size == head);

	if (cnt > remaining)
		cnt = remaining;

	for (int i = 0; i < cnt; ++i) {
		value[i] = queue->entries[tail].entry;
		tail = (tail + 1) % size;
	}

	atomic_store_explicit(&queue->tail, tail, memory_order_release);

	return cnt;
}

#endif // SPSC_H
