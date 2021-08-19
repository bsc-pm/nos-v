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
#include "spsc.h"
#include "generic/spinlock.h"
#include "memory/slab.h"

// TODO: Define a mode where there are multiple queues, but not one per cpu
#define __MPSC_ONE_PER_CPU 1
#define __MPSC_POPS_BEFORE_ROTATE 256

/*
	This is a queue that can accept multiple producers and a single consumer.
	This queue is indeed lock-free between producers, and there is one caveat: If the elements are not
	consumed fast enough, it can fill up (which is fine).
*/

typedef struct mpsc_queue {
	size_t nqueues;
	size_t current;
	spsc_queue_t **queues;
	nosv_spinlock_t qspin __cacheline_aligned;
} mpsc_queue_t;

static inline mpsc_queue_t *mpsc_alloc(size_t nqueues, size_t slots)
{
	mpsc_queue_t *queue = (mpsc_queue_t *)salloc(sizeof(mpsc_queue_t), -1);
	assert(queue);
	queue->queues = (spsc_queue_t **)salloc(sizeof(spsc_queue_t *) * (nqueues + 1), -1);

	assert(nqueues > 0);

	for (int i = 0; i < nqueues + 1; ++i)
		queue->queues[i] = spsc_alloc(slots);

	queue->nqueues = nqueues;
	queue->current = 0;
	nosv_spin_init(&queue->qspin);

	return queue;
}

static inline int mpsc_push(mpsc_queue_t *queue, void *value, int cpu)
{
	assert(value);
	int ret;

	if (unlikely(cpu < 0)) {
		nosv_spin_lock(&queue->qspin);
		ret = spsc_push(queue->queues[queue->nqueues], value);
		nosv_spin_unlock(&queue->qspin);
	} else {
		assert(cpu < queue->nqueues);
		ret = spsc_push(queue->queues[cpu], value);
	}

	return ret;
}

static inline int mpsc_pop_batch(mpsc_queue_t *queue, void **value, int cnt)
{
	const size_t start = queue->current;
	const size_t nqueues = queue->nqueues;
	size_t current = start;
	assert(current <= nqueues);

	int total = 0;
	int ret;

	// First, try to pop from current
	total = spsc_pop_batch(queue->queues[current], value, cnt);
	if (total == cnt) {
		// Rotate the queue
		queue->current = (current + 1) % (nqueues + 1);
		return cnt;
	}

	value = value + total;
	current = (current + 1) % (nqueues + 1);

	while (current != start && total < cnt) {
		ret = spsc_pop_batch(queue->queues[current], value, cnt - total);
		total += ret;
		value += ret;

		if (total == cnt) {
			queue->current = current;
			return cnt;
		}

		current = (current + 1) % (nqueues + 1);
	}

	return total;
}

static inline int mpsc_pop(mpsc_queue_t *queue, void **value)
{
	const size_t start = queue->current;
	const size_t nqueues = queue->nqueues;
	size_t current = start;
	assert(current <= nqueues);

	// First, try to pop from current
	if (spsc_pop(queue->queues[current], value))
		return 1;

	current = (current + 1) % (nqueues + 1);

	while (current != start) {
		if (spsc_pop(queue->queues[current], value)) {
			queue->current = current;
			return 1;
		}

		current = (current + 1) % (nqueues + 1);
	}

	return 0;
}

#endif // mpsc_H
