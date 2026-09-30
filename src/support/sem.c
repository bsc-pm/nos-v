/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2026 Barcelona Supercomputing Center (BSC)
*/

#include <semaphore.h>
#include <stdbool.h>
#include <stdlib.h>

#include "common.h"
#include "instr.h"
#include "nosv.h"
#include "nosv/compat.h"
#include "nosv-internal.h"
#include "generic/clock.h"
#include "generic/list.h"
#include "generic/spinlock.h"

struct nosv_sem {
	list_head_t list;
	list_head_t list_timed;
	nosv_spinlock_t lock;
	unsigned int value;
};

static_assert(sizeof(struct nosv_sem) <= SIZEOF_NOSV_SEM,
	"Exposed semaphore struct sould be at least the size of internal type. Increase exposed struct size accordingly.");

int nosv_sem_init(nosv_sem_t *sem, int pshared, unsigned int value)
{
	struct nosv_sem *s = (struct nosv_sem *) sem;

	if (!s) {
		errno = EINVAL;
		return -1;
	}

	if (value > SEM_VALUE_MAX) {
		errno = EINVAL;
		return -1;
	}

	if (pshared) {
		errno = ENOSYS;
		return -1;
	}

	// Initialize sem object
	list_init(&s->list);
	list_init(&s->list_timed);
	nosv_spin_init(&s->lock);
	s->value = value;
	return 0;
}

int nosv_sem_destroy(nosv_sem_t *sem)
{
	struct nosv_sem *s = (struct nosv_sem *) sem;

	if (!s) {
		errno = EINVAL;
		return -1;
	}

	nosv_spin_lock(&s->lock);
	if (!list_empty(&s->list) || !list_empty(&s->list_timed)) {
		nosv_spin_unlock(&s->lock);
		errno = EBUSY;
		return -1;
	}
	nosv_spin_unlock(&s->lock);
	nosv_spin_destroy(&s->lock);

	return 0;
}

int nosv_sem_getvalue(nosv_sem_t *sem, int *sval)
{
	struct nosv_sem *s = (struct nosv_sem *) sem;

	if (!s || !sval) {
		errno = EINVAL;
		return -1;
	}

	nosv_spin_lock(&s->lock);
	*sval = (int) s->value;
	nosv_spin_unlock(&s->lock);

	return 0;
}

int nosv_sem_post(nosv_sem_t *sem)
{
	struct nosv_sem *s = (struct nosv_sem *) sem;
	nosv_task_t task_unlock = NULL;
	int err = NOSV_SUCCESS;

	if (!s) {
		errno = EINVAL;
		return -1;
	}

	nosv_spin_lock(&s->lock);
	if (s->value == SEM_VALUE_MAX) {
		nosv_spin_unlock(&s->lock);
		errno = EOVERFLOW;
		return -1;
	}

	if (s->value++ == 0) {
		// Unlock someone
		list_head_t *elem;
		elem = list_pop_front(&s->list);
		if (elem) {
			s->value = 0;
			nosv_spin_unlock(&s->lock);
			task_unlock = list_elem(elem, struct nosv_task, list_hook_cond);
			err = nosv_submit(task_unlock, NOSV_SUBMIT_UNLOCKED);
		} else if ((elem = list_pop_front(&s->list_timed))) {
			s->value = 0;
			nosv_spin_unlock(&s->lock);
			task_unlock = list_elem(elem, struct nosv_task, list_hook_cond);
			err = nosv_submit(task_unlock, NOSV_SUBMIT_DEADLINE_WAKE);
		} else {
			nosv_spin_unlock(&s->lock);
		}
	} else {
		nosv_spin_unlock(&s->lock);
	}

	if (unlikely(err != NOSV_SUCCESS))
		nosv_abort("Failed to submit task: %s", nosv_get_error_string(err));

	return 0;
}

__internal int nosv_sem_wait_internal(nosv_sem_t *sem, bool try, const struct timespec *abstime)
{
	struct nosv_sem *s = (struct nosv_sem *) sem;
	int err;

	if (!s) {
		errno = EINVAL;
		return -1;
	}

	nosv_task_t current_task = worker_current_task();

	if (!current_task) {
		errno = ESRCH;
		return -1;
	}

	if (task_is_parallel(current_task)) {
		errno = EINVAL;
		return -1;
	}

	nosv_spin_lock(&s->lock);

	if (s->value) {
		s->value--;
		nosv_spin_unlock(&s->lock);
		return 0;
	}

	// Try-sem
	if (try) {
		// Failed try
		nosv_spin_unlock(&s->lock);
		errno = EAGAIN;
		return -1;
	}

	// Blocking path from now on
	if (abstime == NULL) {
		// Normal lock
		list_add_tail(&s->list, &current_task->list_hook_cond);
		nosv_spin_unlock(&s->lock);
		task_pause(current_task, 1);

		// After being woken up
		return 0;
	}

	// Timed wait
	assert(abstime);
	const uint64_t abstime_ns = abstime->tv_nsec + abstime->tv_sec * 1000000000ULL;
	const uint64_t now_ns = clock_ns_abs();

	if (now_ns >= abstime_ns) {
		// Has passed already, return
		nosv_spin_unlock(&s->lock);
		errno = ETIMEDOUT;
		return -1;
	}

	list_add_tail(&s->list_timed, &current_task->list_hook_cond);
	nosv_spin_unlock(&s->lock);

	uint64_t actual_time_waited = 0;
	uint64_t time_to_wait = abstime_ns - now_ns;

	while (time_to_wait) {
		err = nosv_waitfor(time_to_wait, &actual_time_waited);
		if (unlikely(err != NOSV_SUCCESS)) {
			nosv_abort("Failed to submit deadline task");
		}

		// Waitfor combined with deadline_wake may have spurious wakeups
		// Calculate this just in case
		if (actual_time_waited > time_to_wait)
			time_to_wait = 0;
		else
			time_to_wait -= actual_time_waited;

		nosv_spin_lock(&s->lock);
		// Remove from wait list (if we woke by ourselves)
		// If we aren't on the list, we were woken up (thus, return 0)
		// If time to wait != 0, yet we are on the list, it is a spurious wakeup, and we'll retry
		// If time to wait == 0 and we are on the list, it's a proper timeout
		if (list_node_is_inserted_in_any_list(&current_task->list_hook_cond)) {
			if (time_to_wait == 0) {
				// Timed out
				list_remove(&current_task->list_hook_cond);
				nosv_spin_unlock(&s->lock);
				errno = ETIMEDOUT;
				return -1;
			}
		} else {
			// Exit the loop
			time_to_wait = 0;
		}

		nosv_spin_unlock(&s->lock);
	}

	return 0;
}

int nosv_sem_wait(nosv_sem_t *sem)
{
	return nosv_sem_wait_internal(sem, false, NULL);
}

int nosv_sem_trywait(nosv_sem_t *sem)
{
	return nosv_sem_wait_internal(sem, true, NULL);
}

int nosv_sem_timedwait(nosv_sem_t *sem, const struct timespec *restrict abs_timeout)
{
	return nosv_sem_wait_internal(sem, false, abs_timeout);
}
