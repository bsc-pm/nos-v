/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024-2025 Barcelona Supercomputing Center (BSC)
*/

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "generic/clock.h"
#include "instr.h"
#include "nosv.h"
#include "nosv/compat.h"
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
	int initialized;
};

static_assert(sizeof(struct nosv_cond) <= SIZEOF_NOSV_COND,
	"Exposed barrier struct sould be at least the size of internal type. Increase exposed struct size accordingly.");

int nosv_condattr_init(__maybe_unused nosv_condattr_t *attr)
{
	return NOSV_SUCCESS;
}

int nosv_condattr_destroy(__maybe_unused nosv_condattr_t *attr)
{
	return NOSV_SUCCESS;
}

static inline void nosv_cond_init_internal(struct nosv_cond *c)
{
	list_init(&c->list);
	list_init(&c->list_timed);
	c->initialized = 1;
}

int nosv_cond_init(
	nosv_cond_t *cond,
	__maybe_unused const nosv_condattr_t *condattr)
{
	struct nosv_cond *c = (struct nosv_cond *) cond;

	// Initialize cond object
	nosv_cond_init_internal(c);
	nosv_spin_init(&c->lock);

	return NOSV_SUCCESS;
}

int nosv_cond_destroy(nosv_cond_t *cond) {
	struct nosv_cond *c = (struct nosv_cond *) cond;
	if (!c)
		return NOSV_ERR_INVALID_PARAMETER;
	nosv_spin_lock(&c->lock);
	if (c->initialized && (!list_empty(&c->list) || !list_empty(&c->list_timed)))
		nosv_warn("nosv_cond_destroy called with waiters remaining");

	c->initialized = 0;
	nosv_spin_destroy(&c->lock);
	return NOSV_SUCCESS;
}

int nosv_cond_signal(nosv_cond_t *cond)
{
	struct nosv_cond *c = (struct nosv_cond *) cond;
	if (!c)
		return NOSV_ERR_INVALID_PARAMETER;

	nosv_err_t err = NOSV_SUCCESS;

	instr_cond_signal_enter();

	nosv_spin_lock(&c->lock);
	if (!c->initialized)
		nosv_cond_init_internal(c);

	// First look for a non-timed task
	nosv_task_t task = NULL;
	list_head_t *head;

	if ((head = list_pop_front(&c->list))) { // non-timed
		nosv_spin_unlock(&c->lock);

		task = list_elem(head, struct nosv_task, list_hook_cond);
		err = nosv_submit(task, NOSV_SUBMIT_UNLOCKED);
	} else if ((head = list_pop_front(&c->list_timed))) { // timed
		task = list_elem(head, struct nosv_task, list_hook_cond);
		err = nosv_submit(task, NOSV_SUBMIT_DEADLINE_WAKE);

		nosv_spin_unlock(&c->lock);
	} else { // no task
		nosv_spin_unlock(&c->lock);
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

int nosv_cond_broadcast(nosv_cond_t *cond)
{
	struct nosv_cond *c = (struct nosv_cond *) cond;
	if (!c)
		return NOSV_ERR_INVALID_PARAMETER;

	instr_cond_broadcast_enter();

	// Wake timedtasks inside lock
	nosv_spin_lock(&c->lock);
	if (!c->initialized)
		nosv_cond_init_internal(c);

	list_head_t non_timed;
	list_move_head(&c->list, &non_timed);
	list_submit_cond_tasks(&c->list_timed, NOSV_SUBMIT_DEADLINE_WAKE);
	nosv_spin_unlock(&c->lock);

	// Wake non timed tasks
	list_submit_cond_tasks(&non_timed, NOSV_SUBMIT_UNLOCKED);

	instr_cond_broadcast_exit();

	return NOSV_SUCCESS;
}

static inline int nosv_cond_timedwait_internal(
	nosv_cond_t *cond,
	nosv_mutex_t *nosv_mutex,
	pthread_mutex_t *pthread_mutex,
	const struct timespec *abstime
)
{
	nosv_err_t err = NOSV_SUCCESS;
	nosv_task_t current_task = worker_current_task();
	struct nosv_cond *c = (struct nosv_cond *) cond;

	if (!c || (nosv_mutex == NULL && pthread_mutex == NULL))
		return NOSV_ERR_INVALID_PARAMETER;

	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	instr_cond_wait_enter();

	nosv_spin_lock(&c->lock);
	if (!c->initialized)
		nosv_cond_init_internal(c);

	uint64_t wait_ns = 0;
	if (abstime) {
		const uint64_t abstime_ns = abstime->tv_nsec + abstime->tv_sec * 1000000000ULL;
		const uint64_t now_ns = clock_ns();

		// If time has expired, we return keeping nosv_mutex locked
		if (now_ns > abstime_ns) {
			nosv_spin_unlock(&c->lock);
			instr_cond_wait_exit();

			return NOSV_SUCCESS;
		} else {
			list_add_tail(&c->list_timed, &current_task->list_hook_cond);
			wait_ns = abstime_ns - now_ns;
		}
	} else {
		list_add_tail(&c->list, &current_task->list_hook_cond);
	}
	nosv_spin_unlock(&c->lock);

	if (pthread_mutex) {
		err = pthread_mutex_unlock(pthread_mutex);
	} else {
		err = nosv_mutex_unlock_internal(nosv_mutex, false);
	}
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

		nosv_spin_lock(&c->lock);
		// Remove from wait list (if we woke by ourselves)
		if (list_node_is_inserted_in_any_list(&current_task->list_hook_cond))
			list_remove(&current_task->list_hook_cond);
		nosv_spin_unlock(&c->lock);
	}

	if (pthread_mutex) {
		err = pthread_mutex_lock(pthread_mutex);
	} else {
		err = nosv_mutex_lock(nosv_mutex);
	}

	instr_cond_wait_exit();
	return err;
}

int nosv_cond_timedwait(
	nosv_cond_t *cond,
	nosv_mutex_t *mutex,
	const struct timespec *abstime)
{
	return nosv_cond_timedwait_internal(cond, mutex, NULL, abstime);
}

int nosv_cond_timedwait_pthread(
	nosv_cond_t *cond,
	pthread_mutex_t *mutex,
	const struct timespec *abstime)
{
	return nosv_cond_timedwait_internal(cond, NULL, mutex, abstime);
}

inline int nosv_cond_wait(
	nosv_cond_t *cond,
	nosv_mutex_t *mutex)
{
	return nosv_cond_timedwait(cond, mutex, NULL);
}

inline int nosv_cond_wait_pthread(
	nosv_cond_t *cond,
	pthread_mutex_t *mutex)
{
	return nosv_cond_timedwait_pthread(cond, mutex, NULL);
}
