/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef EVENTQUEUE_H
#define EVENTQUEUE_H

#include "nosv.h"
#include "generic/ringbuffer.h"
#include "generic/signalmutex.h"
#include "hardware/topology.h"
#include "memory/slab.h"
#include "system/tasks.h"

typedef enum creation_event_type {
	Creation,
	Shutdown
} creation_event_type_t;

typedef struct creation_event {
	task_execution_handle_t handle;
	cpu_t *cpu;
	creation_event_type_t type;
} creation_event_t;

typedef struct event_queue {
	nosv_signal_mutex_t lock;
	ring_buffer_t rb;
	void *buffer;
} event_queue_t;

static inline void event_queue_init(event_queue_t *queue)
{
	const size_t cpus = topology_get_level_count(NOSV_TOPO_LEVEL_CPU);
	nosv_signal_mutex_init(&queue->lock);
	queue->buffer = salloc(sizeof(creation_event_t) * cpus * 2, cpu_get_current());
	ring_buffer_init(&queue->rb, sizeof(creation_event_t), cpus * 2, queue->buffer);
}

static inline void event_queue_destroy(event_queue_t *queue)
{
	const size_t cpus = cpus_count();
	sfree(queue->buffer, sizeof(creation_event_t) * cpus * 2, cpu_get_current());
	nosv_signal_mutex_destroy(&queue->lock);
}

static inline int event_queue_put(event_queue_t *queue, creation_event_t *event)
{
	nosv_signal_mutex_lock(&queue->lock);
	int res = ring_buffer_push(&queue->rb, event);
	if (res)
		nosv_signal_mutex_signal(&queue->lock);
	nosv_signal_mutex_unlock(&queue->lock);

	return res;
}

static inline int event_queue_pull(event_queue_t *queue, creation_event_t *event)
{
	nosv_signal_mutex_lock(&queue->lock);

	while (ring_buffer_empty(&queue->rb))
		nosv_signal_mutex_wait(&queue->lock);

	__maybe_unused int success = ring_buffer_pull(&queue->rb, event);
	assert(success);
	nosv_signal_mutex_unlock(&queue->lock);

	return 1;
}

#endif // EVENTQUEUE_H
