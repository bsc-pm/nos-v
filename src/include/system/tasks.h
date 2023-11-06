/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#ifndef TASKS_H
#define TASKS_H

#include "compiler.h"
#include "nosv.h"
#include "nosv-internal.h"
#include "generic/list.h"
#include "generic/spinlock.h"

typedef struct task_type_manager {
	nosv_spinlock_t lock;
	list_head_t types;
} task_type_manager_t;

static inline int task_get_degree(nosv_task_t task)
{
	return atomic_load_explicit(&(task->degree), memory_order_relaxed);
}

static inline int task_is_parallel(nosv_task_t task)
{
	assert(task);

	int degree = task_get_degree(task);
	assert(degree != 0);

	return degree != 1 && degree != -1;
}

typedef struct task_execution_handle {
	// Task in the handle. A value of task == NULL signifies an empty handle
	nosv_task_t task;
	// Execution count when this task was scheduled
	// An execution_id of 0 is only valid for empty handles, otherwise it must be 1 or higher
	uint32_t execution_id;
} task_execution_handle_t;

#define EMPTY_TASK_EXECUTION_HANDLE ((task_execution_handle_t){ .task = NULL, .execution_id = 0 })

__internal void task_execute(task_execution_handle_t handle);
__internal void task_type_manager_init();
__internal void task_type_manager_shutdown();
__internal void task_affinity_init();
__internal list_head_t *task_type_manager_get_list();

#endif // TASKS_H
