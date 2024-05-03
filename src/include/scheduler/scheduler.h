/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdatomic.h>

#include "defaults.h"
#include "compiler.h"
#include "nosv-internal.h"
#include "generic/list.h"
#include "generic/spinlock.h"
#include "generic/tree.h"
#include "scheduler/dtlock.h"
#include "scheduler/governor.h"
#include "scheduler/mpsc.h"

// Flags for the scheduler_get function
#define SCHED_GET_DEFAULT     __ZEROBITS
/* Do not block inside the scheduler */
#define SCHED_GET_NONBLOCKING __BIT(0)
/* Obtain only tasks from different processes */
#define SCHED_GET_EXTERNAL	  __BIT(1)

typedef struct scheduler_queue {
	int priority_enabled;
	union {
		RB_HEAD(priority_tree, nosv_task) tasks_priority;
		list_head_t tasks;
	};
} scheduler_queue_t;

typedef struct scheduler_queue_yield {
	list_head_t tasks;
} scheduler_queue_yield_t;

typedef struct process_scheduler {
	int pid;
	int last_shutdown;
	atomic_int shutdown;
	size_t tasks;
	size_t preferred_affinity_tasks;
	RB_HEAD(deadline_tree, nosv_task) deadline_tasks;
	deadline_t now;
	scheduler_queue_yield_t yield_tasks;
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
	mpsc_queue_t *in_queue;
	timestamp_t *timestamps;
	uint64_t quantum_ns;
	list_head_t queues; // One scheduler_queue per process
	process_scheduler_t *queues_direct[MAX_PIDS]; // Support both lists and random-access
	nosv_spinlock_t in_lock;
	delegation_lock_t dtlock;
	governor_t governor;
	atomic_int deadline_purge;
} scheduler_t;

__internal void scheduler_init(int initialize);
__internal void scheduler_wake(int pid);
__internal void scheduler_shutdown(void);

__internal int scheduler_should_yield(int pid, int cpu, uint64_t *timestamp);
__internal void scheduler_reset_accounting(int pid, int cpu);

// Submit a task to the nOS-V scheduler, but add it to the current task's batch
// if there is one
__internal void scheduler_batch_submit(nosv_task_t task);

// Submit a task to the scheduler ignoring batch submission. Can be used to circumvent
// batch scheduling if needed
__internal void scheduler_submit_single(nosv_task_t task);

// Submit a task group to the scheduler
__internal void scheduler_submit_group(task_group_t *group);

__internal task_execution_handle_t scheduler_get(int cpu, nosv_flags_t flags);
__internal void scheduler_request_deadline_purge(void);
__internal int task_affine(nosv_task_t task, cpu_t *cpu);

#endif // SCHEDULER_H
