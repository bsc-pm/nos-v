/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "defaults.h"
#include "compiler.h"
#include "nosv-internal.h"
#include "generic/heap.h"
#include "generic/list.h"
#include "generic/spinlock.h"
#include "scheduler/dtlock.h"
#include "scheduler/mpsc.h"

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
	delegation_lock_t dtlock;
	size_t tasks;
	size_t served_tasks;
	mpsc_queue_t *in_queue;
	timestamp_t *timestamps;
	uint64_t quantum_ns;
	list_head_t queues; // One scheduler_queue per process
	process_scheduler_t *queues_direct[MAX_PIDS]; // Support both lists and random-access
	nosv_spinlock_t in_lock;
} scheduler_t;

__internal void scheduler_init(int initialize);
__internal void scheduler_shutdown(void);

__internal int scheduler_should_yield(int pid, int cpu, uint64_t *timestamp);
__internal void scheduler_reset_accounting(int pid, int cpu);
__internal void scheduler_submit(nosv_task_t task);
__internal nosv_task_t scheduler_get(int cpu, nosv_flags_t flags);

#endif // SCHEDULER_H
