/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "climits.h"
#include "compiler.h"
#include "nosv-internal.h"
#include "generic/heap.h"
#include "generic/list.h"
#include "generic/spinlock.h"
#include "scheduler/dtlock.h"
#include "scheduler/spsc.h"

#define IN_QUEUE_SIZE 256
#define QUANTUM_NS (20ULL * 1000ULL * 1000ULL)

typedef struct scheduler_queue {
	list_head_t tasks;
} scheduler_queue_t;

typedef struct process_scheduler {
	int pid;
	size_t tasks;
	heap_head_t deadline_tasks;
	deadline_t now;
	scheduler_queue_t *per_cpu_queue_strict;
	scheduler_queue_t *per_cpu_queue_preferred;
	scheduler_queue_t *per_numa_queue_strict;
	scheduler_queue_t *per_numa_queue_preferred;
	scheduler_queue_t queue;
	list_head_t list_hook;
} process_scheduler_t;

typedef struct timestamp {
	uint64_t ts_ns;
	int pid;
} timestamp_t;

typedef struct scheduler {
	size_t tasks;
	delegation_lock_t dtlock;
	nosv_spinlock_t in_lock;
	spsc_queue_t *in_queue;
	list_head_t queues; // One scheduler_queue per process
	process_scheduler_t *queues_direct[MAX_PIDS]; // Support both lists and random-access
	timestamp_t *timestamps;
} scheduler_t;

__internal void scheduler_init(int initialize);

__internal void scheduler_submit(nosv_task_t task);
__internal nosv_task_t scheduler_get(int cpu);

#endif // SCHEDULER_H