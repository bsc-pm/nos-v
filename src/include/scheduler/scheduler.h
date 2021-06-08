/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "climits.h"
#include "compiler.h"
#include "nosv-internal.h"
#include "generic/list.h"
#include "generic/spinlock.h"
#include "scheduler/dtlock.h"
#include "scheduler/spsc.h"

#define IN_QUEUE_SIZE 256
#define QUANTUM_NS (20ULL * 1000ULL * 1000ULL)

#ifdef CLOCK_MONOTONIC_COARSE
#define CLK_SRC CLOCK_MONOTONIC_COARSE
#else
#define CLK_SRC CLOCK_MONOTONIC
#endif

typedef struct scheduler_queue {
	int pid;
	list_head_t tasks;
	list_head_t list_hook;
} scheduler_queue_t;

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
	scheduler_queue_t *queues_direct[MAX_PIDS]; // Support both lists and random-access
	timestamp_t *timestamps;
} scheduler_t;

__internal void scheduler_init(int initialize);

__internal void scheduler_submit(nosv_task_t task);
__internal nosv_task_t scheduler_get(int cpu);

#endif // SCHEDULER_H