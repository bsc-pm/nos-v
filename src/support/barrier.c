/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#include <stdbool.h>
#include <stdlib.h>

#include "common.h"
#include "instr.h"
#include "nosv.h"
#include "nosv-internal.h"
#include "generic/list.h"
#include "generic/spinlock.h"
#include "hardware/threads.h"
#include "scheduler/scheduler.h"
#include "system/taskgroup.h"

struct nosv_barrier {
	task_group_t waiting_tasks;
	nosv_spinlock_t lock;
	unsigned count;
	unsigned towait;
};


int nosv_barrier_init(
	nosv_barrier_t *barrier,
	nosv_flags_t flags,
	unsigned count)
{
	nosv_barrier_t bptr;

	if (flags & ~NOSV_BARRIER_NONE)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!count)
		return NOSV_ERR_INVALID_PARAMETER;

	// Initialize mutex object
	bptr = malloc(sizeof(*bptr));
	if (!bptr)
		return NOSV_ERR_OUT_OF_MEMORY;
	task_group_init(&bptr->waiting_tasks);
	nosv_spin_init(&bptr->lock);
	bptr->count = count;
	bptr->towait = count;
	*barrier = bptr;
	return NOSV_SUCCESS;
}

int nosv_barrier_destroy(nosv_barrier_t barrier)
{
	if (!barrier)
		return NOSV_ERR_INVALID_PARAMETER;
	free(barrier);
	return NOSV_SUCCESS;
}

int nosv_barrier_wait(nosv_barrier_t barrier)
{
	nosv_task_t current_task = worker_current_task();

	if (!barrier)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	instr_barrier_wait_enter();

	nosv_spin_lock(&barrier->lock);
	barrier->towait--;
	if (barrier->towait) {
		// We are not the last one
		// Wait for the other tasks
		task_group_add(&barrier->waiting_tasks, current_task);
		nosv_spin_unlock(&barrier->lock);
		task_pause(current_task, /* use_blocking_count */ 0);
		// We have been unblocked, we can return immediately
	} else {
		// We are the last task
		// First, restore the internal barrier state
		task_group_t waiting_tasks = barrier->waiting_tasks;
		task_group_init(&barrier->waiting_tasks);
		barrier->towait = barrier->count;
		nosv_spin_unlock(&barrier->lock);
		// After this point, the barrier is safe to be reused

		// Second, wake up all waiting tasks.
		if(!task_group_empty(&waiting_tasks))
			scheduler_submit_group(&waiting_tasks);
	}

	instr_barrier_wait_exit();

	return NOSV_SUCCESS;
}
