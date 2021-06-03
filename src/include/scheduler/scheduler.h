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

typedef struct scheduler_queue {
	int pid;
	list_head_t tasks;
	list_head_t list_hook;
} scheduler_queue_t;

typedef struct scheduler {
	size_t tasks;
	delegation_lock_t dtlock;
	nosv_spinlock_t in_lock;
	spsc_queue_t *in_queue;
	list_head_t queues; // One scheduler_queue per process
	scheduler_queue_t *queues_direct[MAX_PIDS]; // Support both lists and random-access
} scheduler_t;

__internal void scheduler_init(int initialize);

__internal void scheduler_submit(nosv_task_t task);
__internal nosv_task_t scheduler_get(int cpu);

#endif // SCHEDULER_H