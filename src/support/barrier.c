/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024-2025 Barcelona Supercomputing Center (BSC)
*/

#include <stdbool.h>
#include <stdlib.h>

#include "common.h"
#include "instr.h"
#include "nosv.h"
#include "nosv/compat.h"
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

static_assert(sizeof(struct nosv_barrier) <= SIZEOF_NOSV_BARRIER,
	"Exposed barrier struct sould be at least the size of internal type. Increase exposed struct size accordingly.");

int nosv_barrierattr_init(nosv_barrierattr_t *attr)
{
	if (!attr)
		return EINVAL;

	return 0;
}

int nosv_barrierattr_destroy(nosv_barrierattr_t *attr)
{
	if (!attr)
		return EINVAL;

	return 0;
}

int nosv_barrier_init(
	nosv_barrier_t *barrier,
	__maybe_unused const nosv_barrierattr_t *attr,
	unsigned count)
{
	if (!barrier)
	return EINVAL;

	struct nosv_barrier *b = (struct nosv_barrier *) barrier;

	if (!count)
		return EINVAL;

	// Initialize barrier object
	task_group_init(&b->waiting_tasks);
	nosv_spin_init(&b->lock);
	b->count = count;
	b->towait = count;
	return 0;
}

int nosv_barrier_destroy(nosv_barrier_t *barrier)
{
	struct nosv_barrier *b = (struct nosv_barrier *) barrier;

	if (!b)
		return EINVAL;

	return 0;
}

int nosv_barrier_wait(nosv_barrier_t *barrier)
{
	struct nosv_barrier *b = (struct nosv_barrier *) barrier;
	nosv_task_t current_task = worker_current_task();

	if (!b)
		return EINVAL;

	if (!current_task)
		return ESRCH;

	instr_barrier_wait_enter();

	nosv_spin_lock(&b->lock);
	b->towait--;
	if (b->towait) {
		// We are not the last one
		// Wait for the other tasks
		task_group_add(&b->waiting_tasks, current_task);
		nosv_spin_unlock(&b->lock);
		task_pause(current_task, /* use_blocking_count */ 0);
		// We have been unblocked, we can return immediately
	} else {
		// We are the last task
		// First, restore the internal barrier state
		task_group_t waiting_tasks = b->waiting_tasks;
		task_group_init(&b->waiting_tasks);
		b->towait = b->count;
		nosv_spin_unlock(&b->lock);
		// After this point, the barrier is safe to be reused

		// Second, wake up all waiting tasks.
		if(!task_group_empty(&waiting_tasks))
			scheduler_submit_group(&waiting_tasks);
	}

	instr_barrier_wait_exit();

	return 0;
}
