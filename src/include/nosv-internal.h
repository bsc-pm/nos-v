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


// Flags
// Flags usage (64 bits): -------- -------- -------- -------- -------- -------- -------- ---sssSC

#define TASK_FLAG_CREATE_PARALLEL      __BIT(0)
// Indicates if the task has to suspend
#define TASK_FLAG_SUSPEND              __BIT(1)
// Suspend Modes
#define TASK_FLAG_SUSPEND_MODE_SUBMIT  __BIT(2)
#define TASK_FLAG_SUSPEND_MODE_TIMEOUT __BIT(3)
#define TASK_FLAG_SUSPEND_MODE_EVENT   __BIT(4)

// Mask to select only the suspend mode
#define TASK_FLAG_SUSPEND_MODE_MASK ( TASK_FLAG_SUSPEND_MODE_SUBMIT | TASK_FLAG_SUSPEND_MODE_TIMEOUT | TASK_FLAG_SUSPEND_MODE_EVENT )

// event_count flag
// Indicates if the task is waiting for events
#define TASK_WAITING_FOR_EVENTS        __BIT(31)

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

typedef struct nosv_task_group {
	struct nosv_task *head_task;
	size_t count;
} task_group_t;

struct nosv_task {
	atomic_uint32_t event_count;
	atomic_int32_t blocking_count;
	size_t metadata;
	struct nosv_task_type *type;
	struct nosv_worker *worker;
	struct nosv_affinity affinity;

	int had_events;
	int priority;
	list_head_t list_hook;
	list_head_t list_hook_cond;

	// Maybe this could be on-demand allocated
	deadline_t deadline;
	_Atomic deadline_state_t deadline_state;

	union {
		yield_t yield;
		RB_ENTRY(nosv_task) tree_hook;
		uint64_t suspend_args;
	};

	// Submit Window (Parent task)
	task_group_t submit_window;
	size_t submit_window_maxsize;

	nosv_task_t wakeup;
	uint64_t taskid;

	// Parallel task support
	// Parallelism degree
	atomic_int32_t degree;

	// Current scheduled count
	uint32_t scheduled_count;
	nosv_flags_t flags;

	// Hardware counters
	task_hwcounters_t *counters;

	// Monitoring statistics
	task_stats_t *stats;
};

static inline nosv_flags_t task_should_suspend(struct nosv_task *task)
{
	return ((task->flags & TASK_FLAG_SUSPEND) != 0);
}

int nosv_mutex_unlock_internal(nosv_mutex_t mutex, char yield_allowed);

#endif // NOSV_INTERNAL_H
