/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2026 Barcelona Supercomputing Center (BSC)
*/

#include <nosv.h>
#include <nosv/compat.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#include "test.h"
#include "common/utils.h"

#define INVALID_CLOCKID ((clockid_t) - 1)

test_t test;

void test_compat_barrier(void)
{
	nosv_barrier_t b;
	nosv_barrierattr_t attr;

	test_check(&test, nosv_barrier_init(&b, NULL, 0) == EINVAL, "barrier_init: EINVAL (count is 0)");

	// WARN: this is not documented in the compat header
	test_check(&test, nosv_barrier_init(&b, &attr, 1) == EINVAL, "barrier_init: EINVAL (attr NOT NULL)");

	// attr == NULL
	test_check(&test, nosv_barrierattr_init(NULL) == EINVAL, "barrierattr_init: EINVAL (attr is NULL)");
	test_check(&test, nosv_barrierattr_destroy(NULL) == EINVAL, "barrierattr_destroy: EINVAL (attr is NULL)");

	// barrier == NULL
	test_check(&test, nosv_barrier_init(NULL, NULL, 0) == EINVAL, "barrier_init: EINVAL (barrier is NULL)");
	test_check(&test, nosv_barrier_destroy(NULL) == EINVAL, "barrier_destroy: EINVAL (barrier is NULL)");
	test_check(&test, nosv_barrier_wait(NULL) == EINVAL, "barrier_wait: EINVAL (barrier is NULL)");

	// Calls that should work outside task context:
	test_check(&test, nosv_barrierattr_init(&attr) == 0, "barrierattr_init: success outside task");
	test_check(&test, nosv_barrierattr_destroy(&attr) == 0, "barrierattr_destroy: success outside task");

	test_check(&test, nosv_barrier_init(&b, NULL, 1) == 0, "barrier_init: success outside task");
	test_check(&test, nosv_barrier_destroy(&b) == 0, "barrier_destroy: success outside task");

	// Forbidden outside task context:
	CHECK(nosv_barrier_init(&b, NULL, 1));
	test_check(&test, nosv_barrier_wait(&b) == ESRCH, "barrier_wait: ESRCH (outside of task context)");
	CHECK(nosv_barrier_destroy(&b));
}

void test_compat_mutex(void)
{
	nosv_mutex_t m;
	nosv_mutexattr_t attr;

	// attr == NULL
	test_check(&test, nosv_mutexattr_init(NULL) == EINVAL, "mutexattr_init: EINVAL (attr is NULL)");
	test_check(&test, nosv_mutexattr_destroy(NULL) == EINVAL, "mutexattr_destroy: EINVAL (attr is NULL)");

	// mutex == NULL
	test_check(&test, nosv_mutex_init(NULL, NULL) == EINVAL, "mutex_init: EINVAL (mutex is NULL)");
	test_check(&test, nosv_mutex_destroy(NULL) == EINVAL, "mutex_destroy: EINVAL (mutex is NULL)");
	test_check(&test, nosv_mutex_lock(NULL) == EINVAL, "mutex_lock: EINVAL (mutex is NULL)");
	test_check(&test, nosv_mutex_trylock(NULL) == EINVAL, "mutex_trylock: EINVAL (mutex is NULL)");
	test_check(&test, nosv_mutex_unlock(NULL) == EINVAL, "mutex_unlock: EINVAL (mutex is NULL)");

	// WARN: this is not documented in the compat header
	test_check(&test, nosv_mutex_init(&m, &attr) == EINVAL, "mutex_init: EINVAL (attr NOT NULL)");

	// Calls that should work outside task context:
	test_check(&test, nosv_mutexattr_init(&attr) == 0, "mutexattr_init: success outside task");
	test_check(&test, nosv_mutexattr_destroy(&attr) == 0, "mutexattr_destroy: success outside task");
	test_check(&test, nosv_mutex_init(&m, NULL) == 0, "mutex_init: success outside task");
	test_check(&test, nosv_mutex_destroy(&m) == 0, "mutex_destroy: success outside task");

	// Forbidden outside task context:
	CHECK(nosv_mutex_init(&m, NULL));
	test_check(&test, nosv_mutex_lock(&m) == ESRCH, "mutex_lock: ESRCH (outside of task context)");
	test_check(&test, nosv_mutex_trylock(&m) == ESRCH, "mutex_trylock: ESRCH (outside of task context)");
	test_check(&test, nosv_mutex_unlock(&m) == ESRCH, "mutex_unlock: ESRCH (outside of task context)");
	CHECK(nosv_mutex_destroy(&m));
}

void test_compat_rwlock(void)
{
	nosv_rwlock_t rw;
	nosv_rwlockattr_t attr;

	// attr == NULL
	test_check(&test, nosv_rwlockattr_init(NULL) == EINVAL, "rwlockattr_init: EINVAL (attr is NULL)");
	test_check(&test, nosv_rwlockattr_destroy(NULL) == EINVAL, "rwlockattr_destroy: EINVAL (attr is NULL)");

	// rwlock == NULL
	test_check(&test, nosv_rwlock_init(NULL, NULL) == EINVAL, "rwlock_init: EINVAL (rwlock is NULL)");
	test_check(&test, nosv_rwlock_destroy(NULL) == EINVAL, "rwlock_destroy: EINVAL (rwlock is NULL)");
	test_check(&test, nosv_rwlock_wrlock(NULL) == EINVAL, "rwlock_wrlock: EINVAL (rwlock is NULL)");
	test_check(&test, nosv_rwlock_rdlock(NULL) == EINVAL, "rwlock_rdlock: EINVAL (rwlock is NULL)");
	test_check(&test, nosv_rwlock_trywrlock(NULL) == EINVAL, "rwlock_trywrlock: EINVAL (rwlock is NULL)");
	test_check(&test, nosv_rwlock_tryrdlock(NULL) == EINVAL, "rwlock_tryrdlock: EINVAL (rwlock is NULL)");
	test_check(&test, nosv_rwlock_unlock(NULL) == EINVAL, "rwlock_unlock: EINVAL (rwlock is NULL)");

	// WARN: this is not documented in the compat header
	test_check(&test, nosv_rwlock_init(&rw, &attr) == EINVAL, "rwlock_init: EINVAL (attr NOT NULL)");

	// Calls that should work outside task context:
	test_check(&test, nosv_rwlockattr_init(&attr) == 0, "rwlockattr_init: success outside task");
	test_check(&test, nosv_rwlockattr_destroy(&attr) == 0, "rwlockattr_destroy: success outside task");
	test_check(&test, nosv_rwlock_init(&rw, NULL) == 0, "rwlock_init: success outside task");
	test_check(&test, nosv_rwlock_destroy(&rw) == 0, "rwlock_destroy: success outside task");

	// Forbidden outside task context:
	CHECK(nosv_rwlock_init(&rw, NULL));
	test_check(&test, nosv_rwlock_wrlock(&rw) == ESRCH, "rwlock_wrlock: ESRCH (outside of task context)");
	test_check(&test, nosv_rwlock_rdlock(&rw) == ESRCH, "rwlock_rdlock: ESRCH (outside of task context)");
	test_check(&test, nosv_rwlock_trywrlock(&rw) == ESRCH, "rwlock_trywrlock: ESRCH (outside of task context)");
	test_check(&test, nosv_rwlock_tryrdlock(&rw) == ESRCH, "rwlock_tryrdlock: ESRCH (outside of task context)");
	test_check(&test, nosv_rwlock_unlock(&rw) == ESRCH, "rwlock_unlock: ESRCH (outside of task context)");
	CHECK(nosv_rwlock_destroy(&rw));
}

void test_compat_cond(void)
{
	nosv_cond_t c;
	nosv_condattr_t attr;
	nosv_mutex_t m = NOSV_MUTEX_INITIALIZER;
	pthread_mutex_t pm = PTHREAD_MUTEX_INITIALIZER;
	clockid_t clk;
	struct timespec ts = {0};

	// attr == NULL
	test_check(&test, nosv_condattr_init(NULL) == EINVAL, "condattr_init: EINVAL (attr is NULL)");
	test_check(&test, nosv_condattr_destroy(NULL) == EINVAL, "condattr_destroy: EINVAL (attr is NULL)");
	test_check(&test, nosv_condattr_getclock(NULL, &clk) == EINVAL, "condattr_getclock: EINVAL (attr is NULL)");
	test_check(&test, nosv_condattr_setclock(NULL, 0) == EINVAL, "condattr_setclock: EINVAL (clock is NULL)");

	// invalid clock/clock ptr
	test_check(&test, nosv_condattr_getclock(&attr, NULL) == EINVAL, "condattr_getclock: EINVAL (clock is NULL)");
	test_check(&test, nosv_condattr_setclock(&attr, INVALID_CLOCKID) == EINVAL, "condattr_setclock: EINVAL (invalid clock_id)");

	// cond == NULL
	test_check(&test, nosv_cond_init(NULL, NULL) == EINVAL, "cond_init: EINVAL (cond is NULL)");
	test_check(&test, nosv_cond_destroy(NULL) == EINVAL, "cond_destroy: EINVAL (cond is NULL)");
	test_check(&test, nosv_cond_signal(NULL) == EINVAL, "cond_signal: EINVAL (cond is NULL)");
	test_check(&test, nosv_cond_broadcast(NULL) == EINVAL, "cond_broadcast: EINVAL (cond is NULL)");

	test_check(&test, nosv_cond_wait(NULL, NULL) == EINVAL, "cond_wait: EINVAL (cond and mutex are NULL)");
	test_check(&test, nosv_cond_timedwait(NULL, NULL, &ts) == EINVAL, "cond_timedwait: EINVAL (cond and mutex are NULL)");
	test_check(&test, nosv_cond_clockwait(NULL, NULL, CLOCK_REALTIME, &ts) == EINVAL, "cond_clockwait: EINVAL (cond and mutex are NULL)");

	test_check(&test, nosv_cond_wait_pthread(NULL, NULL) == EINVAL, "cond_wait_pthread: EINVAL (cond and mutex are NULL)");
	test_check(&test, nosv_cond_timedwait_pthread(NULL, NULL, &ts) == EINVAL, "cond_timedwait_pthread: EINVAL (cond and mutex are NULL)");
	test_check(&test, nosv_cond_clockwait_pthread(NULL, NULL, CLOCK_REALTIME, &ts) == EINVAL, "cond_clockwait_pthread: EINVAL (cond and mutex are NULL)");

	// mutex == NULL (cond valid)
	test_check(&test, nosv_cond_wait(&c, NULL) == EINVAL, "cond_wait: EINVAL (mutex is NULL)");
	test_check(&test, nosv_cond_timedwait(&c, NULL, &ts) == EINVAL, "cond_timedwait: EINVAL (mutex is NULL)");
	test_check(&test, nosv_cond_clockwait(&c, NULL, CLOCK_REALTIME, &ts) == EINVAL, "cond_clockwait: EINVAL (mutex is NULL)");

	test_check(&test, nosv_cond_wait_pthread(&c, NULL) == EINVAL, "cond_wait_pthread: EINVAL (mutex is NULL)");
	test_check(&test, nosv_cond_timedwait_pthread(&c, NULL, &ts) == EINVAL, "cond_timedwait_pthread: EINVAL (mutex is NULL)");
	test_check(&test, nosv_cond_clockwait_pthread(&c, NULL, CLOCK_REALTIME, &ts) == EINVAL, "cond_clockwait_pthread: EINVAL (mutex is NULL)");

	// Calls that should work outside task context:
	test_check(&test, nosv_condattr_init(&attr) == 0, "condattr_init: success outside task");
	test_check(&test, nosv_condattr_destroy(&attr) == 0, "condattr_destroy: success outside task");
	test_check(&test, nosv_cond_init(&c, NULL) == 0, "cond_init: success outside task");
	test_check(&test, nosv_cond_destroy(&c) == 0, "cond_destroy: success outside task");

	// TODO: add waiters
	test_check(&test, nosv_cond_signal(&c) == 0, "cond_signal: success outside task (no waiters)");
	test_check(&test, nosv_cond_broadcast(&c) == 0, "cond_broadcast: success outside task (no waiters)");

	// Forbidden outside task context:
	test_check(&test, nosv_cond_wait(&c, &m) == ESRCH, "cond_wait: ESRCH (outside of task context)");
	test_check(&test, nosv_cond_timedwait(&c, &m, &ts) == ESRCH, "cond_timedwait: ESRCH (outside of task context)");
	test_check(&test, nosv_cond_clockwait(&c, &m, CLOCK_REALTIME, &ts) == ESRCH, "cond_clockwait: ESRCH (outside of task context)");
	test_check(&test, nosv_cond_wait_pthread(&c, &pm) == ESRCH, "cond_wait_pthread: ESRCH (outside of task context)");
	test_check(&test, nosv_cond_timedwait_pthread(&c, &pm, &ts) == ESRCH, "cond_timedwait_pthread: ESRCH (outside of task context)");
	test_check(&test, nosv_cond_clockwait_pthread(&c, &pm, CLOCK_REALTIME, &ts) == ESRCH, "cond_clockwait_pthread: ESRCH (outside of task context)");
}

void test_compat_in_task()
{
	{
		nosv_mutex_t m = NOSV_MUTEX_INITIALIZER;
		CHECK(nosv_mutex_lock(&m));
		test_check(&test, nosv_mutex_destroy(&m) == EBUSY, "mutex_destroy: EBUSY (in use)");
		CHECK(nosv_mutex_unlock(&m));
		test_check(&test, nosv_mutex_destroy(&m) == 0, "mutex_destroy: success after unlock");
	}

	// invalid clock check in clockwait goes after task context, so we can not catch it outside
	{
		nosv_cond_t c = NOSV_COND_INITIALIZER;
		nosv_mutex_t m = NOSV_MUTEX_INITIALIZER;
		pthread_mutex_t pm = PTHREAD_MUTEX_INITIALIZER;
		struct timespec ts = {0};

		test_check(&test, nosv_cond_clockwait(&c, &m, INVALID_CLOCKID, &ts) == EINVAL, "cond_clockwait: EINVAL (invalid clock_id)");
		test_check(&test, nosv_cond_clockwait_pthread(&c, &pm, INVALID_CLOCKID, &ts) == EINVAL, "cond_clockwait_pthread: EINVAL (invalid clock_id)");
	}

	{
		nosv_rwlock_t rw = NOSV_RWLOCK_INITIALIZER;
		CHECK(nosv_rwlock_rdlock(&rw));
		test_check(&test, nosv_rwlock_destroy(&rw) == EBUSY, "rwlock_destroy: EBUSY (in use, read)");
		CHECK(nosv_rwlock_unlock(&rw));
		test_check(&test, nosv_rwlock_destroy(&rw) == 0, "rwlock_destroy: sucess after unlock (read)");
	}

	{
		nosv_rwlock_t rw = NOSV_RWLOCK_INITIALIZER;
		CHECK(nosv_rwlock_wrlock(&rw));
		test_check(&test, nosv_rwlock_destroy(&rw) == EBUSY, "rwlock_destroy: EBUSY (in use, write)");
		CHECK(nosv_rwlock_unlock(&rw));
		test_check(&test, nosv_rwlock_destroy(&rw) == 0, "rwlock_destroy: sucess after unlock (write)");
	}
}

nosv_cond_t global_cond;
nosv_mutex_t global_mutex = NOSV_MUTEX_INITIALIZER;

int waiting = 0;

atomic_int worker_done = 0;

void worker_callback(nosv_task_t task)
{
	CHECK(nosv_mutex_lock(&global_mutex));
	waiting = 1;
	do {
		CHECK(nosv_cond_wait(&global_cond, &global_mutex));
	} while (waiting);
	CHECK(nosv_mutex_unlock(&global_mutex));
}

void worker_complete(nosv_task_t task)
{
	CHECK(nosv_destroy(task, NOSV_DESTROY_NONE));
	atomic_store(&worker_done, 1);
}

void test_compat_with_worker(void)
{
	nosv_task_type_t worker_type;
	nosv_task_t worker;

	CHECK(nosv_cond_init(&global_cond, NULL));

	CHECK(nosv_type_init(&worker_type, worker_callback, NULL, worker_complete, "worker", NULL, NULL, NOSV_TYPE_INIT_NONE));
	CHECK(nosv_create(&worker, worker_type, 0, NOSV_CREATE_NONE));
	CHECK(nosv_submit(worker, NOSV_SUBMIT_NONE));

	// wait for worker to reach and block on cond var
	for (;;) {
		CHECK(nosv_mutex_lock(&global_mutex));
		if (waiting == 1) {
			break;
		}
		CHECK(nosv_mutex_unlock(&global_mutex));
		CHECK(nosv_yield(NOSV_YIELD_NONE));
	}
	waiting = 0;
	test_check(&test, nosv_cond_destroy(&global_cond) == EBUSY, "cond_destroy: EBUSY (in use)");
	CHECK(nosv_cond_signal(&global_cond));
	CHECK(nosv_mutex_unlock(&global_mutex));

	// we should be able to destroy it immediately after signaling
	test_check(&test, nosv_cond_destroy(&global_cond) == 0, "cond_destroy: success after signal");

	// Wait for worker to finish
	while (!atomic_load(&worker_done))
		CHECK(nosv_yield(NOSV_YIELD_NONE));
}

int main()
{
	nosv_task_t task;

	test_init(&test, 90);

	// Init nosv
	CHECK(nosv_init());

	test_compat_barrier();
	test_compat_mutex();
	test_compat_rwlock();
	test_compat_cond();

	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	test_compat_in_task();
	test_compat_with_worker();
	CHECK(nosv_detach(NOSV_DETACH_NONE));

	// Shutdown nosv
	CHECK(nosv_shutdown());
}
