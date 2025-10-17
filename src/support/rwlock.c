/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024-2025 Barcelona Supercomputing Center (BSC)
*/

#include <stdbool.h>
#include <stdlib.h>

#include "common.h"
#include "hardware/topology.h"
#include "instr.h"
#include "nosv.h"
#include "nosv/compat.h"
#include "nosv-internal.h"
#include "generic/list.h"
#include "generic/spinlock.h"
#include "hardware/threads.h"
#include "hardware/topology.h"
#include "scheduler/scheduler.h"

struct nosv_rwlock {
	list_head_t wrlist;
	list_head_t rdlist;
	nosv_spinlock_t lock;
	bool wrtaken;
	unsigned rdtaken_count;
};

static_assert(sizeof(struct nosv_rwlock) <= SIZEOF_NOSV_RWLOCK,
	"Exposed barrier struct sould be at least the size of internal type. Increase exposed struct size accordingly.");

int nosv_rwlock_init(nosv_rwlock_t *rwlock, __maybe_unused const nosv_rwlockattr_t *attr)
{
	struct nosv_rwlock *rwl = (struct nosv_rwlock *)rwlock;

	// Initialize rwlock object
	list_init(&rwl->wrlist);
	list_init(&rwl->rdlist);
	nosv_spin_init(&rwl->lock);
	rwl->wrtaken = false;
	rwl->rdtaken_count = 0;
	return NOSV_SUCCESS;
}

int nosv_rwlock_destroy(nosv_rwlock_t *rwlock)
{
	struct nosv_rwlock *rwl = (struct nosv_rwlock *)rwlock;

	if (!rwl)
		return NOSV_ERR_INVALID_PARAMETER;

	return NOSV_SUCCESS;
}

int nosv_rwlock_wrlock(nosv_rwlock_t *rwlock)
{
	struct nosv_rwlock *rwl = (struct nosv_rwlock *)rwlock;
	nosv_task_t current_task = worker_current_task();

	if (!rwl)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	if (task_is_parallel(current_task))
		return NOSV_ERR_INVALID_OPERATION;

	// Try to take the rwlock
	nosv_spin_lock(&rwl->lock);
	assert(!(rwl->wrtaken && (rwl->rdtaken_count > 0)));
	if (rwl->wrtaken || (rwl->rdtaken_count > 0)) {
		// The rwlock is contended. Add the current task to the list of
		// waiting writer tasks and block.
		list_add_tail(&rwl->wrlist, &current_task->list_hook);
		nosv_spin_unlock(&rwl->lock);
		task_pause(current_task, /* use_blocking_count */ 0);
		// A nosv_rwlock_unlock woke up the current task, the lock is
		// still marked as taken and we can return right now.
	} else {
		// The lock is not taken. Mark the rwlock as taken for write and return.
		rwl->wrtaken = true;
		list_init(&rwl->wrlist);
		list_init(&rwl->rdlist);
		nosv_spin_unlock(&rwl->lock);
	}

	return NOSV_SUCCESS;
}

int nosv_rwlock_rdlock(nosv_rwlock_t *rwlock)
{
	struct nosv_rwlock *rwl = (struct nosv_rwlock *)rwlock;
	nosv_task_t current_task = worker_current_task();

	if (!rwl)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	if (task_is_parallel(current_task))
		return NOSV_ERR_INVALID_OPERATION;

	// Try to take the rwlock
	nosv_spin_lock(&rwl->lock);
	assert(!(rwl->wrtaken && (rwl->rdtaken_count > 0)));
	if (rwl->wrtaken || (rwl->rdtaken_count > 0 && !list_empty(&rwl->wrlist))) {
		// The rwlock is contended. Add the current task to the list of
		// waiting reader tasks and block.
		list_add_tail(&rwl->rdlist, &current_task->list_hook);
		nosv_spin_unlock(&rwl->lock);
		task_pause(current_task, /* use_blocking_count */ 0);
		// A nosv_rwlock_unlock woke up the current task, the lock is
		// still marked as taken and we can return right now.
	} else {
		// The lock is not taken. Mark the rwlock as taken for read and return.
		if (!rwl->rdtaken_count)
			list_init(&rwl->wrlist);
		rwl->rdtaken_count++;
		list_init(&rwl->rdlist);
		nosv_spin_unlock(&rwl->lock);
	}

	return NOSV_SUCCESS;
}

int nosv_rwlock_tryrdlock(nosv_rwlock_t *rwlock)
{
	int rc;
	struct nosv_rwlock *rwl = (struct nosv_rwlock *)rwlock;
	nosv_task_t current_task = worker_current_task();

	if (!rwl)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	// Try to take the rwlock
	nosv_spin_lock(&rwl->lock);
	assert(!(rwl->wrtaken && (rwl->rdtaken_count > 0)));
	if (rwl->wrtaken || (rwl->rdtaken_count > 0 && !list_empty(&rwl->wrlist))) {
		// The rwlock is contended.
		rc = NOSV_ERR_BUSY;
	} else {
		// The lock is not taken. Mark the rwlock as taken for read and return.
		if (!rwl->rdtaken_count)
			list_init(&rwl->wrlist);
		rwl->rdtaken_count++;
		list_init(&rwl->rdlist);
		rc = NOSV_SUCCESS;
	}

	nosv_spin_unlock(&rwl->lock);

	return rc;
}

int nosv_rwlock_trywrlock(nosv_rwlock_t *rwlock)
{
	int rc;
	struct nosv_rwlock *rwl = (struct nosv_rwlock *)rwlock;
	nosv_task_t current_task = worker_current_task();

	if (!rwl)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	// Try to take the rwlock
	nosv_spin_lock(&rwl->lock);
	assert(!(rwl->wrtaken && (rwl->rdtaken_count > 0)));
	if (rwl->wrtaken || rwl->rdtaken_count > 0) {
		// The rwlock is contended.
		nosv_spin_unlock(&rwl->lock);
		rc = NOSV_ERR_BUSY;
	} else {
		// The lock is not taken. Mark the rwlock as taken and return.
		rwl->wrtaken = true;
		list_init(&rwl->wrlist);
		list_init(&rwl->rdlist);
		nosv_spin_unlock(&rwl->lock);
		rc = NOSV_SUCCESS;
	}

	return rc;
}

static inline int nosv_rwlock_unlock_internal(nosv_rwlock_t *rwlock, char yield_allowed)
{
	nosv_task_t task;
	struct nosv_rwlock *rwl = (struct nosv_rwlock *)rwlock;
	nosv_task_t current_task = worker_current_task();
	list_head_t *elem = NULL;

	if (!rwl)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	// Unlock the rwlock
	nosv_spin_lock(&rwl->lock);
	// Detect concurrent reading and writing
	assert(!(rwl->wrtaken && (rwl->rdtaken_count > 0)));
	// Detect excess unlocks
	assert(!(!rwl->wrtaken && (rwl->rdtaken_count == 0)));
	if (rwl->wrtaken) {
		rwl->wrtaken = false;
	} else if (rwl->rdtaken_count > 0) {
		rwl->rdtaken_count--;
	}

	// Only unblock if there are no readers left
	if (rwl->rdtaken_count == 0) {
		// give priority to writers
		elem = list_pop_front(&rwl->wrlist);
		if (elem) {
			rwl->wrtaken = true;
		} else {
			elem = list_pop_front(&rwl->rdlist);
			while (elem && !list_empty(&rwl->rdlist)) {
				rwl->rdtaken_count++;
				task = list_elem(elem, struct nosv_task, list_hook);
				scheduler_submit_single(task);
				elem = list_pop_front(&rwl->rdlist);
			}

			if (elem)
				rwl->rdtaken_count++;
		}
	}

	nosv_spin_unlock(&rwl->lock);

	if (elem) {
		// There is at least one waiting writer task to get the rwlock. Unblock
		// the task and transfer the rwlock ownership to it (we keep the
		// rwlock flagged as "taken").

		// If the next task to run can run in the current core, switch
		// the current task for the next task in order to speed up
		// unlocking contended tasks
		task = list_elem(elem, struct nosv_task, list_hook);

		cpu_t *current_cpu = cpu_ptr(cpu_get_current());

		if (yield_allowed && task_affine(task, current_cpu)) {
			// Since the task is affine, yield the current core to the unblocked task to speed things up
			// and forego the scheduler

			task_execution_handle_t handle = {
				.task = task,
				.execution_id = 1 // Doesn't really matter, since task is blocked
			};
			worker_yield_to(handle);
		} else {
			// The task was not affine, so we only submit it and
			// expect that someone else will run it.
			scheduler_submit_single(task);
		}
	}

	return NOSV_SUCCESS;
}

int nosv_rwlock_unlock(nosv_rwlock_t *rwlock) {
	return nosv_rwlock_unlock_internal(rwlock, 1);
}
