/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef MPSC_H
#define MPSC_H

#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "compiler.h"
#include "spsc.h"
#include "config/config.h"
#include "generic/spinlock.h"
#include "memory/slab.h"

/*
	This is a queue that can accept multiple producers and a single consumer.
	This queue is indeed lock-free between producers, and there is one caveat: If the elements are not
	consumed fast enough, it can fill up (which is fine).
*/

typedef struct mpsc_subqueue {
	nosv_spinlock_t qspin __cacheline_aligned;
	spsc_queue_t *queue;
} mspc_subqueue_t;

typedef struct mpsc_queue {
	size_t nqueues;
	size_t cpus_per_queue;
	size_t current;
	mspc_subqueue_t *queues;
} mpsc_queue_t;

static inline mpsc_queue_t *mpsc_alloc(size_t nqueues, size_t slots)
{
	mpsc_queue_t *queue = (mpsc_queue_t *)salloc(sizeof(mpsc_queue_t), -1);
	assert(queue);
	nqueues = (nqueues + nosv_config.sched_cpus_per_queue - 1) / nosv_config.sched_cpus_per_queue;
	queue->queues = (mspc_subqueue_t *)salloc(sizeof(mspc_subqueue_t) * (nqueues + 1), -1);

	assert(nqueues > 0);

	for (int i = 0; i < nqueues + 1; ++i) {
		queue->queues[i].queue = spsc_alloc(slots);
		nosv_spin_init(&queue->queues[i].qspin);
	}

	queue->nqueues = nqueues;
	queue->cpus_per_queue = nosv_config.sched_cpus_per_queue;
	queue->current = 0;

	return queue;
}

static inline int mpsc_push(mpsc_queue_t *queue, void *value, int cpu)
{
	assert(value);
	size_t q;
	int ret;

	if (unlikely(cpu < 0)) {
		q = queue->nqueues;
	} else {
		assert(cpu < queue->nqueues * queue->cpus_per_queue);
		q = cpu / queue->cpus_per_queue;
	}

	nosv_spin_lock(&queue->queues[q].qspin);
	ret = spsc_push(queue->queues[q].queue, value);
	nosv_spin_unlock(&queue->queues[q].qspin);

	return ret;
}

static inline size_t mpsc_pop_batch(mpsc_queue_t *queue, void **value, size_t cnt)
{
	const size_t start = queue->current;
	const size_t nqueues = queue->nqueues;
	size_t current = start;
	assert(current <= nqueues);

	size_t total = 0;
	size_t ret;

	do {
		ret = spsc_pop_batch(queue->queues[current].queue, value, cnt - total);
		total += ret;
		value += ret;
		current = (current + 1) % (nqueues + 1);
	} while (current != start && total < cnt);

	// We always advance queue->current, to prevent getting stuck extracting from a single queue
	queue->current = current;

	return total;
}

#endif // MPSC_H
