/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024-2026 Barcelona Supercomputing Center (BSC)
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
	clockid_t clock_id;
	int initialized;
};

struct nosv_condattr {
	clockid_t clock_id;
};

static_assert(sizeof(struct nosv_condattr) <= SIZEOF_NOSV_CONDATTR,
	"Exposed condattr struct should be at least the size of internal type. Increase exposed struct size accordingly");

static_assert(sizeof(struct nosv_cond) <= SIZEOF_NOSV_COND,
	"Exposed barrier struct sould be at least the size of internal type. Increase exposed struct size accordingly.");

int nosv_condattr_init(nosv_condattr_t *attr)
{
	if (!attr)
		return EINVAL;

	struct nosv_condattr *a = (struct nosv_condattr *) attr;
	a->clock_id = CLOCK_REALTIME;

	return 0;
}

int nosv_condattr_destroy(nosv_condattr_t *attr)
{
	if (!attr)
		return EINVAL;

	return 0;
}

int nosv_condattr_getclock(const nosv_condattr_t *restrict attr, clockid_t *restrict clock_id)
{
	if (clock_id == NULL || attr == NULL)
		return EINVAL;

	struct nosv_condattr *a = (struct nosv_condattr *) attr;
	*clock_id = a->clock_id;

	return 0;
}

int nosv_condattr_setclock(nosv_condattr_t *attr, clockid_t clock_id)
{
	if (attr == NULL)
		return EINVAL;

	// Verify that clock_id is valid
	uint64_t now_ns;
	if (clock_ns_from_id(clock_id, &now_ns)) {
		return EINVAL;
	}

	struct nosv_condattr *a = (struct nosv_condattr *) attr;
	a->clock_id = clock_id;

	return 0;
}

static inline void nosv_cond_init_internal(struct nosv_cond *c)
{
	list_init(&c->list);
	list_init(&c->list_timed);
	c->initialized = 1;
	c->clock_id = CLOCK_REALTIME;
}

int nosv_cond_init(
	nosv_cond_t *cond,
	__maybe_unused const nosv_condattr_t *condattr)
{
	if (!cond)
		return EINVAL;

	struct nosv_cond *c = (struct nosv_cond *) cond;

	// Initialize cond object
	nosv_cond_init_internal(c);
	nosv_spin_init(&c->lock);

	if (condattr != NULL) {
		struct nosv_condattr *a = (struct nosv_condattr *) condattr;
		c->clock_id = a->clock_id;
	}

	return 0;
}

int nosv_cond_destroy(nosv_cond_t *cond) {
	struct nosv_cond *c = (struct nosv_cond *) cond;
	if (!c)
		return EINVAL;

	nosv_spin_lock(&c->lock);
	if (c->initialized && (!list_empty(&c->list) || !list_empty(&c->list_timed))) {
		nosv_spin_unlock(&c->lock);
		return EBUSY; // currently in use
	}
	c->initialized = 0;
	nosv_spin_unlock(&c->lock);

	nosv_spin_destroy(&c->lock);

	return 0;
}

int nosv_cond_signal(nosv_cond_t *cond)
{
	struct nosv_cond *c = (struct nosv_cond *) cond;
	if (!c)
		return EINVAL;

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
		int ret = nosv_submit(task, NOSV_SUBMIT_UNLOCKED);
		if (ret)
			nosv_abort("Internal error when submitting a blocked task");
	} else if ((head = list_pop_front(&c->list_timed))) { // timed
		task = list_elem(head, struct nosv_task, list_hook_cond);
		int ret = nosv_submit(task, NOSV_SUBMIT_DEADLINE_WAKE);
		if (ret)
			nosv_abort("Internal error when submitting a deadline task");

		nosv_spin_unlock(&c->lock);
	} else { // no task
		nosv_spin_unlock(&c->lock);
	}

	instr_cond_signal_exit();
	return 0;
}

static inline void list_submit_cond_tasks(list_head_t *list, nosv_flags_t flags) {
	list_head_t *head;
	nosv_task_t task;
	if (list_empty(list))
		return;

	list_for_each_pop(head, list) {
		task = list_elem(head, struct nosv_task, list_hook_cond);
		int ret = nosv_submit(task, flags);
		if (ret)
			nosv_abort("Internal error submitting blocked tasks");
	}
	assert(list_empty(list));
}

int nosv_cond_broadcast(nosv_cond_t *cond)
{
	struct nosv_cond *c = (struct nosv_cond *) cond;
	if (!c)
		return EINVAL;

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

	return 0;
}

static inline int nosv_cond_clockwait_internal(
	struct nosv_cond *c,
	nosv_mutex_t *nosv_mutex,
	pthread_mutex_t *pthread_mutex,
	clockid_t clock_id,
	const struct timespec *abstime)
{
	int err = 0;
	int timedout = 0;
	nosv_task_t current_task = worker_current_task();

	if (!c || (nosv_mutex == NULL && pthread_mutex == NULL))
		return EINVAL;

	if (!current_task)
		return ESRCH;

	if (task_is_parallel(current_task))
		return EINVAL;

	instr_cond_wait_enter();

	nosv_spin_lock(&c->lock);
	if (!c->initialized)
		nosv_cond_init_internal(c);

	uint64_t wait_ns = 0;
	if (abstime) {
		const uint64_t abstime_ns = abstime->tv_nsec + abstime->tv_sec * 1000000000ULL;
		uint64_t now_ns;
		if (unlikely(clock_ns_from_id(clock_id, &now_ns))) {
			// Invalid clock
			nosv_spin_unlock(&c->lock);
			instr_cond_wait_exit();
			return EINVAL;
		}

		// Skip list_timed addition if deadline is expired
		if (now_ns >= abstime_ns) {
			timedout = 1;
			// We cannot return here, since pthread semantics require releasing
			// and re-acquiring the mutex even when timedout
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

	if (unlikely(err != 0)) {
		nosv_abort("Failed to unlock mutex");
	}

	if (!abstime) { // nosv_cond_wait (not timed)
		err = nosv_pause(NOSV_PAUSE_NONE);
		if (unlikely(err != NOSV_SUCCESS)) {
			instr_cond_wait_exit();
			nosv_abort("Unexpected internal error when pausing the task");
		}
	} else if (!timedout) { // timed wait (non-expired)
		assert(wait_ns);

		err = nosv_waitfor(wait_ns, NULL);
		if (unlikely(err != NOSV_SUCCESS)) {
			nosv_abort("Failed to submit deadline task");
		}

		nosv_spin_lock(&c->lock);
		if (list_node_is_inserted_in_any_list(&current_task->list_hook_cond)) {
			// If we woke by ourselves iff we are still in the list, clean up
			list_remove(&current_task->list_hook_cond);
			timedout = 1;
		}
		nosv_spin_unlock(&c->lock);
	}

	if (pthread_mutex) {
		err = pthread_mutex_lock(pthread_mutex);
	} else {
		err = nosv_mutex_lock(nosv_mutex);
	}

	if (unlikely(err != 0)) {
		nosv_abort("Failed to lock mutex");
	}

	instr_cond_wait_exit();
	return timedout ? ETIMEDOUT : 0;
}

int nosv_cond_timedwait(
	nosv_cond_t *cond,
	nosv_mutex_t *mutex,
	const struct timespec *abstime)
{
	if (cond == NULL)
		return EINVAL;

	struct nosv_cond *c = (struct nosv_cond *) cond;
	return nosv_cond_clockwait_internal(c, mutex, NULL, c->clock_id, abstime);
}

int nosv_cond_timedwait_pthread(
	nosv_cond_t *cond,
	pthread_mutex_t *mutex,
	const struct timespec *abstime)
{
	if (cond == NULL)
		return EINVAL;

	struct nosv_cond *c = (struct nosv_cond *) cond;
	return nosv_cond_clockwait_internal(c, NULL, mutex, c->clock_id, abstime);
}

int nosv_cond_clockwait(
	nosv_cond_t *cond,
	nosv_mutex_t *mutex,
	clockid_t clock_id,
	const struct timespec *abstime)
{
	return nosv_cond_clockwait_internal((struct nosv_cond *) cond, mutex, NULL, clock_id, abstime);
}

int nosv_cond_clockwait_pthread(
	nosv_cond_t *cond,
	pthread_mutex_t *mutex,
	clockid_t clock_id,
	const struct timespec *abstime)
{
	return nosv_cond_clockwait_internal((struct nosv_cond *) cond, NULL, mutex, clock_id, abstime);
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
