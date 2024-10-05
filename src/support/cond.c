/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "generic/clock.h"
#include "instr.h"
#include "nosv.h"
#include "nosv-internal.h"
#include "generic/list.h"
#include "generic/spinlock.h"
#include "hardware/threads.h"
#include "scheduler/scheduler.h"

struct nosv_cond {
	list_head_t list;
	// We need a separate list for timed tasks since we need to distinguish
	// them when waking the tasks
	list_head_t list_timed;
	nosv_spinlock_t lock;
};

int nosv_cond_init(
	nosv_cond_t *cond,
	nosv_flags_t flags)
{
	nosv_cond_t cptr;

	if (flags & ~NOSV_COND_NONE)
		return NOSV_ERR_INVALID_PARAMETER;

	// Initialize cond object
	cptr = malloc(sizeof(*cptr));
	if (!cptr)
		return NOSV_ERR_OUT_OF_MEMORY;
	list_init(&cptr->list);
	list_init(&cptr->list_timed);
	nosv_spin_init(&cptr->lock);

	*cond = cptr;
	return NOSV_SUCCESS;
}

int nosv_cond_destroy(nosv_cond_t cond) {
	if (!cond)
		return NOSV_ERR_INVALID_PARAMETER;
	nosv_spin_lock(&cond->lock);
	if (!list_empty(&cond->list) || !list_empty(&cond->list_timed))
		nosv_warn("nosv_cond_destroy called with waiters remaining");
	nosv_spin_destroy(&cond->lock);
	free(cond);
	return NOSV_SUCCESS;
}

int nosv_cond_signal(nosv_cond_t cond)
{
	if (!cond)
		return NOSV_ERR_INVALID_PARAMETER;

	nosv_err_t err = NOSV_SUCCESS;

	instr_cond_signal_enter();

	nosv_spin_lock(&cond->lock);

	// First look for a non-timed task
	nosv_task_t task = NULL;
	list_head_t *head;

	if ((head = list_pop_front(&cond->list))) { // non-timed
		nosv_spin_unlock(&cond->lock);

		task = list_elem(head, struct nosv_task, list_hook_cond);
		err = nosv_submit(task, NOSV_SUBMIT_UNLOCKED);
	} else if ((head = list_pop_front(&cond->list_timed))) { // timed
		task = list_elem(head, struct nosv_task, list_hook_cond);
		err = nosv_submit(task, NOSV_SUBMIT_DEADLINE_WAKE);

		nosv_spin_unlock(&cond->lock);
	} else { // no task
		nosv_spin_unlock(&cond->lock);
	}

	instr_cond_signal_exit();
	return err;
}

static inline void list_submit_cond_tasks(list_head_t *list, nosv_flags_t flags) {
	list_head_t *head;
	nosv_task_t task;
	if (list_empty(list))
		return;

	list_for_each_pop(head, list) {
		task = list_elem(head, struct nosv_task, list_hook_cond);
		nosv_submit(task, flags);
	}
	assert(list_empty(list));
}

int nosv_cond_broadcast(nosv_cond_t cond)
{
	if (!cond)
		return NOSV_ERR_INVALID_PARAMETER;

	instr_cond_broadcast_enter();

	nosv_spin_lock(&cond->lock);

	// Wake tasks
	list_submit_cond_tasks(&cond->list, NOSV_SUBMIT_UNLOCKED);
	list_submit_cond_tasks(&cond->list_timed, NOSV_SUBMIT_DEADLINE_WAKE);

	nosv_spin_unlock(&cond->lock);

	instr_cond_broadcast_exit();

	return NOSV_SUCCESS;
}

int nosv_cond_timedwait(
	nosv_cond_t cond,
	nosv_mutex_t mutex,
	const struct timespec *abstime)
{
	nosv_err_t err = NOSV_SUCCESS;
	nosv_task_t current_task = worker_current_task();

	if (!cond)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	instr_cond_wait_enter();

	nosv_spin_lock(&cond->lock);
	uint64_t wait_ns = 0;
	if (abstime) {
		const uint64_t abstime_ns = abstime->tv_nsec + abstime->tv_sec * 1000000000ULL;
		const uint64_t now_ns = clock_ns();

		// If time has expired, we return keeping nosv_mutex locked
		if (now_ns > abstime_ns) {
			nosv_spin_unlock(&cond->lock);
			instr_cond_wait_exit();

			return NOSV_SUCCESS;
		} else {
			list_add_tail(&cond->list_timed, &current_task->list_hook_cond);
			wait_ns = abstime_ns - now_ns;
		}
	} else {
		list_add_tail(&cond->list, &current_task->list_hook_cond);
	}
	nosv_spin_unlock(&cond->lock);

	err = nosv_mutex_unlock(mutex);
	if (unlikely(err != NOSV_SUCCESS)) {
		nosv_abort("Failed to unlock nosv mutex");
	}

	if (!abstime) { // nosv_cond_wait (not timed)
		err = nosv_pause(NOSV_PAUSE_NONE);
		if (unlikely(err != NOSV_SUCCESS)) {
			instr_cond_wait_exit();
			return err;
		}
	} else { // timed wait
		assert(wait_ns);

		err = nosv_waitfor(wait_ns, NULL);
		if (unlikely(err != NOSV_SUCCESS)) {
			nosv_abort("Failed to submit deadline task");
		}

		nosv_spin_lock(&cond->lock);
		// Remove from wait list (if we woke by ourselves)
		if (list_node_is_inserted_in_any_list(&current_task->list_hook_cond))
			list_remove(&current_task->list_hook_cond);
		nosv_spin_unlock(&cond->lock);
	}

	err = nosv_mutex_lock(mutex);

	instr_cond_wait_exit();
	return err;
}

inline int nosv_cond_wait(
	nosv_cond_t cond,
	nosv_mutex_t mutex)
{
	return nosv_cond_timedwait(cond, mutex, NULL);
}
