/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2026 Barcelona Supercomputing Center (BSC)
*/

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <time.h>

#include <nosv.h>
#include <nosv/compat.h>

#include "test.h"
#include "common/utils.h"

#define NTASKS 16
#define TEST_TIMEOUT 4000

enum wait_mode {
	WAIT,
	TIMEDWAIT,
};

struct metadata {
	enum wait_mode mode;
	struct timespec deadline;
	int result;
	int error;
};

test_t test;
nosv_sem_t sem;
nosv_task_type_t task_type;
nosv_task_t tasks[NTASKS];

atomic_int started;
atomic_int completed;

static void check_error(int result, int expected, const char *message)
{
	int error = errno;
	test_check(&test, result == -1 && error == expected,
		"%s (result %d, errno %d, expected %d)", message, result, error, expected);
}

static struct timespec deadline_after_ms(long milliseconds)
{
	struct timespec deadline;
	CHECK(clock_gettime(CLOCK_REALTIME, &deadline));
	deadline.tv_sec += milliseconds / 1000;
	deadline.tv_nsec += (milliseconds % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000L;
	}
	return deadline;
}

static void task_run(nosv_task_t task)
{
	struct metadata *meta = nosv_get_task_metadata(task);
	atomic_fetch_add(&started, 1);

	errno = 0;
	if (meta->mode == TIMEDWAIT)
		meta->result = nosv_sem_timedwait(&sem, &meta->deadline);
	else
		meta->result = nosv_sem_wait(&sem);
	meta->error = errno;
}

static void task_completed(nosv_task_t task)
{
	(void) task;
	atomic_fetch_add(&completed, 1);
}

static void create_tasks(int ntasks, enum wait_mode mode, struct timespec deadline)
{
	atomic_store(&started, 0);
	atomic_store(&completed, 0);

	for (int i = 0; i < ntasks; i++) {
		CHECK(nosv_create(&tasks[i], task_type, sizeof(struct metadata), NOSV_CREATE_NONE));
		struct metadata *meta = nosv_get_task_metadata(tasks[i]);
		meta->mode = mode;
		meta->deadline = deadline;
		meta->result = -1;
		meta->error = 0;
	}
}

static void submit_tasks(int ntasks)
{
	for (int i = 0; i < ntasks; i++)
		CHECK(nosv_submit(tasks[i], NOSV_SUBMIT_NONE));
}

static void destroy_tasks(int ntasks)
{
	for (int i = 0; i < ntasks; i++)
		CHECK(nosv_destroy(tasks[i], NOSV_DESTROY_NONE));
}

static int count_results(int ntasks, int result, int error)
{
	int count = 0;
	for (int i = 0; i < ntasks; i++) {
		struct metadata *meta = nosv_get_task_metadata(tasks[i]);
		if (meta->result == result && meta->error == error)
			count++;
	}
	return count;
}

static void test_counting(void)
{
	int value = -1;
	CHECK(nosv_sem_init(&sem, 0, 2));

	test_check(&test, nosv_sem_getvalue(&sem, &value) == 0 && value == 2,
		"Getvalue reports the initial token count");
	test_check(&test, nosv_sem_trywait(&sem) == 0, "Trywait consumes the first initial token");
	test_check(&test, nosv_sem_trywait(&sem) == 0, "Trywait consumes the second initial token");
	test_check(&test, nosv_sem_getvalue(&sem, &value) == 0 && value == 0,
		"Getvalue reports zero after tokens are consumed");
	check_error(nosv_sem_trywait(&sem), EAGAIN, "Trywait fails when the semaphore is empty");
	test_check(&test, nosv_sem_post(&sem) == 0, "Post increments an empty semaphore");
	test_check(&test, nosv_sem_getvalue(&sem, &value) == 0 && value == 1,
		"Getvalue reports a posted token");
	test_check(&test, nosv_sem_post(&sem) == 0, "Post increments a non-empty semaphore");
	test_check(&test, nosv_sem_getvalue(&sem, &value) == 0 && value == 2,
		"Getvalue reports a posted token");
	test_check(&test, nosv_sem_trywait(&sem) == 0, "Trywait consumes a posted token");
	test_check(&test, nosv_sem_trywait(&sem) == 0, "Trywait consumes a posted token");
	check_error(nosv_sem_trywait(&sem), EAGAIN, "The posted token was consumed exactly once");

	struct timespec past = deadline_after_ms(0);
	past.tv_sec--;
	check_error(nosv_sem_timedwait(&sem, &past), ETIMEDOUT,
		"Timedwait fails when its deadline has already passed");

	CHECK(nosv_sem_post(&sem));
	test_check(&test, nosv_sem_timedwait(&sem, &past) == 0,
		"Timedwait consumes an available token despite a past deadline");
	check_error(nosv_sem_trywait(&sem), EAGAIN, "Timedwait consumed the available token");

	CHECK(nosv_sem_destroy(&sem));
}

static void test_blocking_waiters(void)
{
	struct timespec unused = {0};
	CHECK(nosv_sem_init(&sem, 0, 0));
	create_tasks(NTASKS, WAIT, unused);
	submit_tasks(NTASKS);

	test_check_waitfor(&test, atomic_load(&started) == NTASKS, TEST_TIMEOUT,
		"All blocking waiters started");
	test_check(&test, atomic_load(&completed) == 0,
		"No blocking waiter completes before a post");
	test_check_waitfor(&test, nosv_sem_destroy(&sem) == -1 && errno == EBUSY,
			TEST_TIMEOUT, "Destroy rejects a semaphore with blocking waiters");
	int value = -1;
	test_check(&test, nosv_sem_getvalue(&sem, &value) == 0 && value == 0,
		"Getvalue reports zero with blocking waiters");

	for (int i = 1; i <= NTASKS; i++) {
		CHECK(nosv_sem_post(&sem));
		test_check_waitfor(&test, atomic_load(&completed) == i, TEST_TIMEOUT,
			"Post %d wakes exactly one blocking waiter", i);
	}

	test_check(&test, count_results(NTASKS, 0, 0) == NTASKS,
		"All blocking waits return successfully");
	destroy_tasks(NTASKS);
	CHECK(nosv_sem_destroy(&sem));
}

struct post_result {
	int result;
	int error;
};

static void *external_post(void *argument)
{
	struct post_result *result = argument;
	errno = 0;
	result->result = nosv_sem_post(&sem);
	result->error = errno;
	return NULL;
}

static void test_external_post(void)
{
	struct timespec unused = {0};
	struct post_result post_result = {-1, 0};
	pthread_t thread;

	CHECK(nosv_sem_init(&sem, 0, 0));
	create_tasks(1, WAIT, unused);
	submit_tasks(1);
	test_check_waitfor(&test, atomic_load(&started) == 1, TEST_TIMEOUT,
		"Waiter for the external post started");

	CHECK(pthread_create(&thread, NULL, external_post, &post_result));
	CHECK(pthread_join(thread, NULL));
	test_check(&test, post_result.result == 0 && post_result.error == 0,
		"Post succeeds outside task context");
	test_check_waitfor(&test, atomic_load(&completed) == 1, TEST_TIMEOUT,
		"External post wakes a blocking task");
	test_check(&test, count_results(1, 0, 0) == 1,
		"Wait woken by an external post returns successfully");

	destroy_tasks(1);
	CHECK(nosv_sem_destroy(&sem));
}

static void test_timed_waiters(void)
{
	CHECK(nosv_sem_init(&sem, 0, 0));
	create_tasks(NTASKS, TIMEDWAIT, deadline_after_ms(100));
	submit_tasks(NTASKS);

	test_check_waitfor(&test, atomic_load(&started) == NTASKS, TEST_TIMEOUT,
		"All expiring timed waiters started");
	test_check_waitfor(&test, atomic_load(&completed) == NTASKS, TEST_TIMEOUT,
		"All timed waiters complete after their deadline");
	test_check(&test, count_results(NTASKS, -1, ETIMEDOUT) == NTASKS,
		"All expired timed waits return ETIMEDOUT");

	destroy_tasks(NTASKS);
	CHECK(nosv_sem_destroy(&sem));

	CHECK(nosv_sem_init(&sem, 0, 0));
	create_tasks(NTASKS, TIMEDWAIT, deadline_after_ms(2 * TEST_TIMEOUT));
	submit_tasks(NTASKS);
	test_check_waitfor(&test, atomic_load(&started) == NTASKS, TEST_TIMEOUT,
		"All posted timed waiters started");
	test_check_waitfor(&test, nosv_sem_destroy(&sem) == -1 && errno == EBUSY,
			TEST_TIMEOUT, "Destroy rejects a semaphore with timed waiters");
	int value = -1;
	test_check(&test, nosv_sem_getvalue(&sem, &value) == 0 && value == 0,
		"Getvalue reports zero with timed waiters");

	for (int i = 0; i < NTASKS; i++)
		CHECK(nosv_sem_post(&sem));

	test_check_waitfor(&test, atomic_load(&completed) == NTASKS, TEST_TIMEOUT,
		"Posts wake all timed waiters before their deadline");
	test_check(&test, count_results(NTASKS, 0, 0) == NTASKS,
		"All posted timed waits return successfully");

	destroy_tasks(NTASKS);
	CHECK(nosv_sem_destroy(&sem));
}

struct parallel_results {
	int wait;
	int trywait;
	int timedwait;
	int post;
};

static atomic_int parallel_completed;
static struct parallel_results parallel_results;

static void parallel_run(nosv_task_t task)
{
	struct timespec deadline = deadline_after_ms(1000);
	(void) task;

	errno = 0;
	parallel_results.wait = nosv_sem_wait(&sem) == -1 && errno == EINVAL;
	errno = 0;
	parallel_results.trywait = nosv_sem_trywait(&sem) == -1 && errno == EINVAL;
	errno = 0;
	parallel_results.timedwait =
		nosv_sem_timedwait(&sem, &deadline) == -1 && errno == EINVAL;
	errno = 0;
	parallel_results.post = nosv_sem_post(&sem) == 0 && errno == 0;
}

static void parallel_completed_callback(nosv_task_t task)
{
	(void) task;
	atomic_store(&parallel_completed, 1);
}

static void test_parallel_task(void)
{
	nosv_task_type_t parallel_type;
	nosv_task_t parallel_task;

	CHECK(nosv_sem_init(&sem, 0, 0));
	CHECK(nosv_type_init(&parallel_type, parallel_run, NULL,
		parallel_completed_callback, "sem-parallel", NULL, NULL, NOSV_TYPE_INIT_NONE));
	CHECK(nosv_create(&parallel_task, parallel_type, 0, NOSV_CREATE_PARALLEL));
	nosv_set_task_degree(parallel_task, 1);
	CHECK(nosv_submit(parallel_task, NOSV_SUBMIT_NONE));

	test_check_waitfor(&test, atomic_load(&parallel_completed), TEST_TIMEOUT,
		"Parallel semaphore test task completed");
	test_check(&test, parallel_results.wait, "Wait rejects a parallel task");
	test_check(&test, parallel_results.trywait, "Trywait rejects a parallel task");
	test_check(&test, parallel_results.timedwait, "Timedwait rejects a parallel task");
	test_check(&test, parallel_results.post, "Post succeeds from a parallel task");

	CHECK(nosv_destroy(parallel_task, NOSV_DESTROY_NONE));
	CHECK(nosv_type_destroy(parallel_type, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_sem_destroy(&sem));
}

int main(void)
{
	nosv_task_t main_task;
	struct timespec deadline = deadline_after_ms(1000);
	int value = -1;

	test_init(&test, 69);

	check_error(nosv_sem_init(NULL, 0, 0), EINVAL, "Init rejects a NULL semaphore");
	if ((unsigned long long) SEM_VALUE_MAX < UINT_MAX)
		check_error(nosv_sem_init(&sem, 0, (unsigned int) SEM_VALUE_MAX + 1U), EINVAL,
			"Init rejects a value above SEM_VALUE_MAX");
	else
		test_skip(&test, "SEM_VALUE_MAX equals UINT_MAX",
			"Init rejects a value above SEM_VALUE_MAX");
	check_error(nosv_sem_init(&sem, 1, 0), ENOSYS,
		"Init rejects process-shared semaphores");

	CHECK(nosv_sem_init(&sem, 0, SEM_VALUE_MAX));
	test_check(&test, nosv_sem_getvalue(&sem, &value) == 0 && value == SEM_VALUE_MAX,
		"Getvalue works outside task context and reports SEM_VALUE_MAX");
	check_error(nosv_sem_post(&sem), EOVERFLOW,
		"Post rejects incrementing SEM_VALUE_MAX");
	CHECK(nosv_sem_destroy(&sem));

	CHECK(nosv_sem_init(&sem, 0, 0));
	check_error(nosv_sem_destroy(NULL), EINVAL, "Destroy rejects a NULL semaphore");
	check_error(nosv_sem_getvalue(NULL, &value), EINVAL, "Getvalue rejects a NULL semaphore");
	check_error(nosv_sem_getvalue(&sem, NULL), EINVAL, "Getvalue rejects a NULL output pointer");
	check_error(nosv_sem_post(NULL), EINVAL, "Post rejects a NULL semaphore");
	check_error(nosv_sem_wait(NULL), EINVAL, "Wait rejects a NULL semaphore");
	check_error(nosv_sem_trywait(NULL), EINVAL, "Trywait rejects a NULL semaphore");
	check_error(nosv_sem_timedwait(NULL, &deadline), EINVAL,
		"Timedwait rejects a NULL semaphore");
	check_error(nosv_sem_wait(&sem), ESRCH, "Wait rejects calls outside task context");
	check_error(nosv_sem_trywait(&sem), ESRCH, "Trywait rejects calls outside task context");
	check_error(nosv_sem_timedwait(&sem, &deadline), ESRCH,
		"Timedwait rejects calls outside task context");
	test_check(&test, nosv_sem_post(&sem) == 0, "Post succeeds outside task context");
	CHECK(nosv_sem_destroy(&sem));

	CHECK(nosv_init());
	CHECK(nosv_attach(&main_task, NULL, "main", NOSV_ATTACH_NONE));
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_completed,
		"sem-waiter", NULL, NULL, NOSV_TYPE_INIT_NONE));

	test_counting();
	test_blocking_waiters();
	test_external_post();
	test_timed_waiters();
	test_parallel_task();

	CHECK(nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());
	test_end(&test);
	return 0;
}
