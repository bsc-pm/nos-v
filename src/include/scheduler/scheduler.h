/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "compiler.h"
#include "generic/list.h"
#include "scheduler/dtlock.h"

typedef struct scheduler_queue {
	int pid;
	list_head_t tasks;
} scheduler_queue_t;

typedef struct scheduler {
	delegation_lock_t dtlock;
	// mpsc_queue_t in_queue;
	list_head_t queues; // One scheduler_queue per process
} scheduler_t;

#endif // SCHEDULER_H