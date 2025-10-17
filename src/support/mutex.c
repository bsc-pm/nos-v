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

struct nosv_mutex {
	list_head_t list;
	nosv_spinlock_t lock;
	bool taken;
#ifndef NDEBUG
	pid_t owner;
#endif
};

static_assert(sizeof(struct nosv_mutex) <= SIZEOF_NOSV_MUTEX,
	"Exposed barrier struct sould be at least the size of internal type. Increase exposed struct size accordingly.");

int nosv_mutexattr_init(nosv_mutexattr_t *attr)
{
	if (!attr)
		return EINVAL;

	return 0;
}

int nosv_mutexattr_destroy(nosv_mutexattr_t *attr)
{
	if (!attr)
		return EINVAL;

	return 0;
}

int nosv_mutex_init(nosv_mutex_t *mutex, __maybe_unused const nosv_mutexattr_t *mutexattr)
{
	struct nosv_mutex *m = (struct nosv_mutex *) mutex;

	if (!m)
		return EINVAL;

	// Initialize mutex object
	// m->list is NOT initialized with the static initializer, which means it will have to be initialized at a later point again
	// to make sure
	list_init(&m->list);
	nosv_spin_init(&m->lock);
	m->taken = false;
#ifndef NDEBUG
		m->owner = 0;
#endif
	return 0;
}

int nosv_mutex_destroy(nosv_mutex_t *mutex)
{
	struct nosv_mutex *m = (struct nosv_mutex *) mutex;
	if (!m)
		return EINVAL;
	return 0;
}

int nosv_mutex_lock(nosv_mutex_t *mutex)
{
	struct nosv_mutex *m = (struct nosv_mutex *) mutex;
	nosv_task_t current_task = worker_current_task();

	if (!m)
		return EINVAL;

	if (!current_task)
		return ESRCH;

	if (task_is_parallel(current_task))
		return EINVAL;

	instr_mutex_lock_enter();

	// Try to take the mutex
	nosv_spin_lock(&m->lock);
	if (m->taken) {
		// The mutex is contended. Add the current task to the list of
		// waiting tasks and block.
		list_add_tail(&m->list, &current_task->list_hook);
#ifndef NDEBUG
		assert(m->owner != 0 && m->owner != worker_current()->tid);
#endif
		nosv_spin_unlock(&m->lock);
		task_pause(current_task, /* use_blocking_count */ 0);
		// A nosv_mutex_unlock woke up the current task, the lock is
		// still marked as taken and we can return right now.
	} else {
		// The lock is not taken. Mark the mutex as taken and return.
		m->taken = true;
		list_init(&m->list);
#ifndef NDEBUG
		m->owner = worker_current()->tid;
#endif
		nosv_spin_unlock(&m->lock);
	}

	instr_mutex_lock_exit();

	return 0;
}

int nosv_mutex_trylock(nosv_mutex_t *mutex)
{
	struct nosv_mutex *m = (struct nosv_mutex *) mutex;
	int rc;
	nosv_task_t current_task = worker_current_task();

	if (!m)
		return EINVAL;

	if (!current_task)
		return ESRCH;

	if (task_is_parallel(current_task))
		return EINVAL;

	instr_mutex_trylock_enter();

	// Try to take the mutex
	nosv_spin_lock(&m->lock);
	if (m->taken) {
		// The mutex is contended.
		nosv_spin_unlock(&m->lock);
		rc = EBUSY;
	} else {
		// The lock is not taken. Mark the mutex as taken and return.
		m->taken = true;
		list_init(&m->list);
#ifndef NDEBUG
		m->owner = worker_current()->tid;
#endif
		nosv_spin_unlock(&m->lock);
		rc = 0;
	}

	instr_mutex_trylock_exit();

	return rc;
}

__internal int nosv_mutex_unlock_internal(nosv_mutex_t *mutex, char yield_allowed)
{
	nosv_task_t task;
	list_head_t *elem;
	struct nosv_mutex *m = (struct nosv_mutex *) mutex;
	nosv_task_t current_task = worker_current_task();

	if (!m)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	instr_mutex_unlock_enter();

	// Unlock the mutex
	nosv_spin_lock(&m->lock);
	elem = list_pop_front(&m->list);
	if (!elem) {
		// There are no waiting tasks for this mutex, mark the mutex as
		// not taken and return.
		m->taken = false;
#ifndef NDEBUG
		m->owner = 0;
#endif
		nosv_spin_unlock(&m->lock);
	} else {
#ifndef NDEBUG
		m->owner = list_elem(elem, struct nosv_task, list_hook)->worker->tid;
#endif
		// There is at least one waiting tasks to get the mutex. Unblock
		// the task and transfer the mutex ownership to it (we keep the
		// mutex flagged as "taken").
		nosv_spin_unlock(&m->lock);

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

	instr_mutex_unlock_exit();

	return NOSV_SUCCESS;
}

int nosv_mutex_unlock(nosv_mutex_t *mutex) {
	return nosv_mutex_unlock_internal(mutex, 1);
}
