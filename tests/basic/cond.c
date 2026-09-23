/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024-2026 Barcelona Supercomputing Center (BSC)
*/

#include <nosv.h>
#include <nosv/compat.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "test.h"
#include "common/utils.h"


#define NTASKS 100
#define TEST_TIMEOUT 4000

enum cond_test_flag {
	BROADCAST    = __ZEROBITS,
	SIGNAL		 = __BIT(0), // 0 -> broadcast, 1 -> signal
	NOWAIT		 = __BIT(1), // Don't wait for timed tasks to expire
	PTHREAD		 = __BIT(2), // Use pthread_mutex instead of nosv_mutex
};

typedef enum cond_test_flag cond_test_flag_t;

test_t test;
nosv_task_type_t task_type;
nosv_task_t tasks[NTASKS];

atomic_uint track_init;

// the lower 16 bits are used to count expirations (timed-out), upper 16 bits
// count the total completions
atomic_uint_fast32_t track_completion_status;
static_assert(NTASKS < UINT16_MAX, "Number of tasks cannot exceed 2^16");

int ready = 0;

nosv_cond_t cond = NOSV_COND_INITIALIZER;
nosv_mutex_t mutex = NOSV_MUTEX_INITIALIZER;
pthread_mutex_t pthread_mutex = PTHREAD_MUTEX_INITIALIZER;

struct metadata {
	int tid;
	struct timespec *deadline;
	cond_test_flag_t flags;
	int expired;
};

static inline void lock(const cond_test_flag_t flags) {
	if (flags & PTHREAD) CHECK(pthread_mutex_lock(&pthread_mutex));
	else CHECK(nosv_mutex_lock(&mutex));
}

static inline void unlock(const cond_test_flag_t flags) {
	if (flags & PTHREAD) CHECK(pthread_mutex_unlock(&pthread_mutex));
	else CHECK(nosv_mutex_unlock(&mutex));
}

void task_run(nosv_task_t task)
{
	atomic_fetch_add_explicit(&track_init, 1, memory_order_relaxed);
	struct metadata *meta = nosv_get_task_metadata(task);
	struct timespec *deadline = meta->deadline;
	meta->expired = 0;

	lock(meta->flags);
	while (ready <= 0) {
		if (deadline) {
			int err;
			if (meta->flags & PTHREAD)
				err = nosv_cond_timedwait_pthread(&cond, &pthread_mutex, deadline);
			else
				err = nosv_cond_timedwait(&cond, &mutex, deadline);

			if (err == ETIMEDOUT) {
				meta->expired = 1;
				break;
			} else {
				CHECK(err);
			}
		} else {
			if (meta->flags & PTHREAD)
				CHECK(nosv_cond_wait_pthread(&cond, &pthread_mutex));
			else
				CHECK(nosv_cond_wait(&cond, &mutex));
		}
	}
	ready--;
	unlock(meta->flags);
}

void task_comp(nosv_task_t task)
{
	struct metadata *meta = nosv_get_task_metadata(task);
	// Mark this task as finished
	atomic_fetch_add_explicit(&track_completion_status, 1 << 16 | !!(meta->expired), memory_order_relaxed);
}

typedef struct {
	uint16_t total, expired;
} completion_pair_t;


static inline completion_pair_t fetch_completion_vars()
{
	completion_pair_t res;
	uint_least64_t completed_bits = atomic_load_explicit(&track_completion_status, memory_order_relaxed);
	res.total = completed_bits >> 16;
	res.expired = completed_bits & 0xFFFF;
	return res;
}

void add_ms(struct timespec *deadline, int64_t ms)
{
	deadline->tv_sec += ms / 1000; // ms -> s
	ms %= 1000;

	deadline->tv_nsec += ms * 1000000L; // ms -> ns

	if (deadline->tv_nsec >= 1000000000L) {
		deadline->tv_nsec -= 1000000000L;
		deadline->tv_sec++;
	} else if (deadline->tv_nsec < 0) {
		deadline->tv_nsec += 1000000000L;
		deadline->tv_sec--;
	}
}

void test_cond(const char *pref, const char *label, const cond_test_flag_t flags, const int timed_tasks, const long time_ms)
{
	char msg[255];
	sprintf(msg, "%s-%s", pref, label);
	atomic_store_explicit(&track_init, 0, memory_order_relaxed);
	atomic_store_explicit(&track_completion_status, 0, memory_order_relaxed);
	ready = 0;

	struct timespec deadline;
	if (timed_tasks) {
		clock_gettime(CLOCK_REALTIME, &deadline);
		add_ms(&deadline, time_ms);
	}

	for (int i = 0; i < NTASKS; i++) {
		CHECK(nosv_create(&tasks[i], task_type, sizeof(struct metadata), NOSV_CREATE_NONE));
		struct metadata *meta = nosv_get_task_metadata(tasks[i]);
		meta->tid = i;
		meta->flags = flags;

		// Only use timedwait for the first n tasks
		meta->deadline = i < timed_tasks ? &deadline : NULL;
	}

	// broadcast into empty queue, should do nothing
	lock(flags);
	CHECK(nosv_cond_broadcast(&cond));
	unlock(flags);

	// Submit all tasks. Each task will call nosv_cond_wait
	for (int i = 0; i < NTASKS; i++)
		CHECK(nosv_submit(tasks[i], NOSV_SUBMIT_NONE));

	// Wait until all tasks start
	while (NTASKS != atomic_load_explicit(&track_init, memory_order_relaxed))
		CHECK(nosv_yield(NOSV_YIELD_NONE));

	// Wait until all timed tasks finish
	completion_pair_t completion_counters = {0, 0};

	if (flags & NOWAIT) {
		test_ok(&test, "%s: not waiting for timedwait tasks", msg);
		completion_counters = fetch_completion_vars();
		test_check(&test, completion_counters.expired <= timed_tasks && completion_counters.total == completion_counters.expired, "%s: %d out of %d tasks returned ETIMEDOUT and no tasks have been signaled", msg, completion_counters.expired, timed_tasks);
	} else {
		test_check_waitfor(&test,
			timed_tasks == (completion_counters = fetch_completion_vars()).expired,
			TEST_TIMEOUT,
			"%s: %d timed tasks finished, expected %d (%d remain, ready = %d)", msg, completion_counters.expired,
			timed_tasks, NTASKS - timed_tasks, ready);

		uint signaled = completion_counters.total - completion_counters.expired;
		test_check(&test, 0 == signaled, "%s: %d tasks were signaled (expeteced none)", msg, signaled, timed_tasks);
	}

	if (flags & SIGNAL) {
		// At this point, all tasks should have blocked into the scheduler
		for (int i = 1; i <= NTASKS; i++) {
			// Unblock one task
			lock(flags);
			ready++;
			CHECK(nosv_cond_signal(&cond));
			unlock(flags);

			uint expired;
			// Wait for that task to finish
			while (i > (completion_counters = fetch_completion_vars()).total)
				CHECK(nosv_yield(NOSV_YIELD_NONE));

			// Ensure that only one task finished
			if (timed_tasks + i < completion_counters.total) {
				test_fail(&test, "%s: Tasks were not unblocked in order: expected <= %d but found %d\n",
					msg, i, completion_counters.total);
				exit(1);
			}
		}
	} else { // broadcast
		lock(flags);
		ready += NTASKS;
		CHECK(nosv_cond_broadcast(&cond));
		unlock(flags);
	}

	// Wait until all tasks finish
	test_check_waitfor(&test, NTASKS == (completion_counters = fetch_completion_vars()).total,
		TEST_TIMEOUT, "%s: %d unlocked correctly, expected %d (timedout %d)", msg, completion_counters.total, NTASKS, completion_counters.expired);

	lock(flags);
	if (ready)
		test_error(&test, "%s: ready = %d, expected 0", msg, ready);
	unlock(flags);

	for (int i = 0; i < NTASKS; i++) {
		CHECK(nosv_destroy(tasks[i], NOSV_DESTROY_NONE));
	}
}

void init_objs() {
	// Prepare the global cond and mutex
	CHECK(nosv_cond_init(&cond, NULL));
	CHECK(nosv_mutex_init(&mutex, NULL));
	CHECK(pthread_mutex_init(&pthread_mutex, NULL));
}

void free_objs() {
	// Free the cond object and mutex
	CHECK(nosv_cond_destroy(&cond));
	CHECK(nosv_mutex_destroy(&mutex));
	CHECK(pthread_mutex_destroy(&pthread_mutex));
}

#define NVARIANTS 4

void test_condattr(void)
{
	nosv_cond_t cond;
	nosv_condattr_t attr;

	CHECK(nosv_condattr_init(&attr));

	clockid_t clock_id;
	CHECK(nosv_condattr_getclock(&attr, &clock_id));

	test_check(&test, clock_id == CLOCK_REALTIME, "condattr: default clock_id is CLOCK_REALTIME");
	test_check(&test, nosv_condattr_setclock(&attr, 1337) == EINVAL, "condattr: returns EINVAL on invalid clock");

	CHECK(nosv_condattr_setclock(&attr, CLOCK_MONOTONIC));
	CHECK(nosv_condattr_getclock(&attr, &clock_id));
	test_check(&test, clock_id == CLOCK_MONOTONIC, "condattr: getclock returns CLOCK_MONOTONIC after setclock");

	CHECK(nosv_condattr_destroy(&attr));
}

void timespec_diff(struct timespec *start, struct timespec *end, struct timespec *result)
{
	if ((end->tv_nsec - start->tv_nsec) < 0) {
		result->tv_sec = end->tv_sec - start->tv_sec - 1;
		result->tv_nsec = end->tv_nsec - start->tv_nsec + 1000000000ULL;
	} else {
		result->tv_sec = end->tv_sec - start->tv_sec;
		result->tv_nsec = end->tv_nsec - start->tv_nsec;
	}
}

void test_clock_timing(const char *msg, clockid_t clock_id, uint64_t time_ms)
{
	nosv_condattr_t attr;
	CHECK(nosv_condattr_init(&attr));
	CHECK(nosv_condattr_setclock(&attr, clock_id));

	nosv_cond_t cond;
	CHECK(nosv_cond_init(&cond, &attr));

	struct timespec deadline;
	CHECK(clock_gettime(clock_id, &deadline));
	add_ms(&deadline, time_ms);

	nosv_mutex_t m = NOSV_MUTEX_INITIALIZER;
	CHECK(nosv_mutex_lock(&m));

	// wait until timeout
	int err;
	while (!(err = nosv_cond_timedwait(&cond, &m, &deadline)))
		;
	if (err != ETIMEDOUT)
		CHECK(err);

	CHECK(nosv_mutex_unlock(&m));

	struct timespec now;
	CHECK(clock_gettime(clock_id, &now));

	if (now.tv_sec < deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec < deadline.tv_nsec)) {
		test_fail(&test, "%s: woke up before deadline", msg);
	} else {
		test_ok(&test, "%s: timing ok", msg);
	}

	CHECK(nosv_condattr_destroy(&attr));
	CHECK(nosv_cond_destroy(&cond));
}

void test_clock_timing_clockwait(const char *msg, clockid_t clock_id, uint64_t time_ms)
{
	nosv_cond_t cond = NOSV_COND_INITIALIZER;
	nosv_mutex_t m = NOSV_MUTEX_INITIALIZER;

	struct timespec deadline;
	CHECK(clock_gettime(clock_id, &deadline));
	add_ms(&deadline, time_ms);

	CHECK(nosv_mutex_lock(&m));

	// wait until timeout
	int err;
	while (!(err = nosv_cond_clockwait(&cond, &m, clock_id, &deadline)))
		;
	if (err != ETIMEDOUT)
		CHECK(err);

	CHECK(nosv_mutex_unlock(&m));

	struct timespec now;
	CHECK(clock_gettime(clock_id, &now));

	if (now.tv_sec < deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec < deadline.tv_nsec)) {
		test_fail(&test, "%s: woke up before deadline", msg);
	} else {
		test_ok(&test, "%s: timing ok", msg);
	}
}

int main()
{
	nosv_task_t task;
	const int timeout_ms = 250;

	test_init(&test, 3 + 4 + 3 * 7 * NVARIANTS);

	// Init nosv
	CHECK(nosv_init());
	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));

	test_condattr();

	test_clock_timing("timedwait-REALTIME", CLOCK_REALTIME, timeout_ms);
	test_clock_timing("timedwait-MONOTONIC", CLOCK_MONOTONIC, timeout_ms);

	test_clock_timing_clockwait("clockwait-REALTIME", CLOCK_REALTIME, timeout_ms);
	test_clock_timing_clockwait("clockwait-MONOTONIC", CLOCK_MONOTONIC, timeout_ms);

	cond_test_flag_t variants[NVARIANTS] = {BROADCAST, SIGNAL, BROADCAST | PTHREAD, SIGNAL | PTHREAD};
	char *labels[NVARIANTS] = {"broadcast", "signal", "broadcast-pthread", "signal-pthread"};

	for (int i = 0; i < NVARIANTS; ++i) {
		// The first iteration will use static initializers
		if (i)
			init_objs();

		const cond_test_flag_t flags = variants[i];
		const char *label = labels[i];

		// Run the first test
		test_cond(label, "basic", flags, 0, 0);
		// Repeat, this time reusing the cond
		test_cond(label, "reuse", flags, 0, 0);

		// All tasks with timed
		test_cond(label, "timed-full", flags, NTASKS, timeout_ms);
		// Half the tasks timed
		test_cond(label, "timed-half", flags, NTASKS/2, timeout_ms);
		// All tasks timed, with expired deadline (100 seconds ago)
		test_cond(label, "timed-expired", flags, NTASKS, -100000);

		// Half the tasks timed, but we don't wait for them
		test_cond(label, "timed-half-nowait", flags | NOWAIT, NTASKS/2, timeout_ms);
		// All the tasks timed, but we don't wait for them
		test_cond(label, "timed-full-nowait", flags | NOWAIT, NTASKS, timeout_ms);

		// Reset objects for signal test
		free_objs();
	}

	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());
}

