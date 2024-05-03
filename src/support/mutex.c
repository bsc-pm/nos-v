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

struct nosv_mutex {
	list_head_t list;
	nosv_spinlock_t lock;
	bool taken;
};

int nosv_mutex_init(nosv_mutex_t *mutex, nosv_flags_t flags)
{
	nosv_mutex_t mptr;

	if (flags & ~NOSV_MUTEX_NONE)
		return NOSV_ERR_INVALID_PARAMETER;

	// Initialize mutex object
	mptr = malloc(sizeof(*mptr));
	if (!mptr)
		return NOSV_ERR_OUT_OF_MEMORY;
	list_init(&mptr->list);
	nosv_spin_init(&mptr->lock);
	mptr->taken = false;
	*mutex = mptr;
	return NOSV_SUCCESS;
}

int nosv_mutex_destroy(nosv_mutex_t mutex)
{
	if (!mutex)
		return NOSV_ERR_INVALID_PARAMETER;
	// Free the mutex without checking its state. If it is taken, the
	// result is undefined
	free(mutex);
	return NOSV_SUCCESS;
}

int nosv_mutex_lock(nosv_mutex_t mutex)
{
	nosv_task_t current_task = worker_current_task();

	if (!mutex)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	instr_mutex_lock_enter();

	// Try to take the mutex
	nosv_spin_lock(&mutex->lock);
	if (mutex->taken) {
		// The mutex is contended. Add the current task to the list of
		// waiting tasks and block.
		list_add_tail(&mutex->list, &current_task->list_hook);
		nosv_spin_unlock(&mutex->lock);
		nosv_pause(NOSV_PAUSE_NONE);
		// A nosv_mutex_unlock woke up the current task, the lock is
		// still marked as taken and we can return right now.
	} else {
		// The lock is not taken. Mark the mutex as taken and return.
		mutex->taken = true;
		nosv_spin_unlock(&mutex->lock);
	}

	instr_mutex_lock_exit();

	return NOSV_SUCCESS;
}

int nosv_mutex_trylock(nosv_mutex_t mutex)
{
	int rc;
	nosv_task_t current_task = worker_current_task();

	if (!mutex)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	instr_mutex_trylock_enter();

	// Try to take the mutex
	nosv_spin_lock(&mutex->lock);
	if (mutex->taken) {
		// The mutex is contended.
		nosv_spin_unlock(&mutex->lock);
		rc = NOSV_ERR_BUSY;
	} else {
		// The lock is not taken. Mark the mutex as taken and return.
		mutex->taken = true;
		nosv_spin_unlock(&mutex->lock);
		rc = NOSV_SUCCESS;
	}

	instr_mutex_trylock_exit();

	return rc;
}

int nosv_mutex_unlock(nosv_mutex_t mutex)
{
	nosv_task_t task;
	list_head_t *elem;
	nosv_task_t current_task = worker_current_task();

	if (!mutex)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	instr_mutex_unlock_enter();

	// Unlock the mutex
	nosv_spin_lock(&mutex->lock);
	elem = list_pop_head(&mutex->list);
	if (!elem) {
		// There are no waiting tasks for this mutex, mark the mutex as
		// not taken and return.
		mutex->taken = false;
		nosv_spin_unlock(&mutex->lock);
	} else {
		// There is at least one waiting tasks to get the mutex. Unblock
		// the task and transfer the mutex ownership to it (we keep the
		// mutex flagged as "taken").
		nosv_spin_unlock(&mutex->lock);

		// If the next task to run can run in the current core, switch
		// the current task for the next task in order to speed up
		// unlocking contended tasks
		task = list_elem(elem, struct nosv_task, list_hook);
		if (!worker_yield_if_affine(current_task, task)) {
			// The task was not affine, so we only submit it and
			// expect that someone else will run it.
			nosv_submit(task, NOSV_SUBMIT_UNLOCKED);
		}
	}

	instr_mutex_unlock_exit();

	return NOSV_SUCCESS;
}
