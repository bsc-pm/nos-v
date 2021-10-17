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
#define DEFAULT_QUANTUM_NS (20ULL * 1000ULL * 1000ULL)

// Flags for the scheduler_get function
#define SCHED_GET_DEFAULT     __ZEROBITS
#define SCHED_GET_NONBLOCKING __BIT(0)

typedef struct scheduler_queue {
	list_head_t tasks;
} scheduler_queue_t;

typedef struct process_scheduler {
	int pid;
	size_t tasks;
	size_t preferred_affinity_tasks;
	heap_head_t deadline_tasks;
	deadline_t now;
	scheduler_queue_t yield_tasks;
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
	size_t served_tasks;
	delegation_lock_t dtlock;
	nosv_spinlock_t in_lock;
	spsc_queue_t *in_queue;
	list_head_t queues; // One scheduler_queue per process
	process_scheduler_t *queues_direct[MAX_PIDS]; // Support both lists and random-access
	timestamp_t *timestamps;
	uint64_t quantum_ns;
} scheduler_t;

__internal void scheduler_init(int initialize);

__internal int scheduler_should_yield(int pid, int cpu, uint64_t *timestamp);
__internal void scheduler_reset_accounting(int pid, int cpu);
__internal void scheduler_submit(nosv_task_t task);
__internal nosv_task_t scheduler_get(int cpu, nosv_flags_t flags);

#endif // SCHEDULER_H
