/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_INTERNAL_H
#define NOSV_INTERNAL_H

#include <stdatomic.h>

#include "compiler.h"
#include "defaults.h"
#include "generic/list.h"
#include "generic/tree.h"
#include "nosv.h"
#include "nosv/affinity.h"
#include "nosv/hwinfo.h"

struct nosv_worker;
typedef uint64_t deadline_t;
typedef enum {
	NOSV_DEADLINE_NONE,
	NOSV_DEADLINE_PENDING,
	NOSV_DEADLINE_WAITING,
	NOSV_DEADLINE_READY,
} deadline_state_t;
typedef size_t yield_t;
typedef struct task_hwcounters task_hwcounters_t;
typedef struct tasktype_stats tasktype_stats_t;
typedef struct task_stats task_stats_t;

struct nosv_task_type {
	nosv_task_run_callback_t run_callback;
	nosv_task_end_callback_t end_callback;
	nosv_task_completed_callback_t completed_callback;
	void *metadata;
	const char *label;
	int pid;
	uint32_t typeid;
	uint64_t (*get_cost)(nosv_task_t);
	tasktype_stats_t *stats;
	list_head_t list_hook;
};

struct nosv_task {
	atomic_uint32_t event_count;
	atomic_uint32_t blocking_count;
	size_t metadata;
	struct nosv_task_type *type;
	struct nosv_worker *worker;
	struct nosv_affinity affinity;

	int priority;
	list_head_t list_hook;

	// Maybe this could be on-demand allocated
	deadline_t deadline;
	_Atomic deadline_state_t deadline_state;

	union {
		yield_t yield;
		RB_ENTRY(nosv_task) tree_hook;
	};

	nosv_task_t wakeup;
	uint64_t taskid;

	// Parallel task support
	// Parallelism degree
	atomic_int32_t degree;

	// Current execution count
	uint32_t execution_count;

	// Hardware counters
	task_hwcounters_t *counters;

	// Monitoring statistics
	task_stats_t *stats;
};

#endif // NOSV_INTERNAL_H
