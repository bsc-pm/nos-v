/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#include <nosv.h>
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

atomic_uint completed[NTASKS];
atomic_uint track_init;
atomic_uint track_completed;

int ready = 0;

nosv_cond_t cond;
nosv_mutex_t mutex;
pthread_mutex_t pthread_mutex;

struct metadata {
	int tid;
	struct timespec *deadline;
	cond_test_flag_t flags;
};

static inline void lock(const cond_test_flag_t flags) {
	if (flags & PTHREAD) CHECK(pthread_mutex_lock(&pthread_mutex));
	else CHECK(nosv_mutex_lock(mutex));
}

static inline void unlock(const cond_test_flag_t flags) {
	if (flags & PTHREAD) CHECK(pthread_mutex_unlock(&pthread_mutex));
	else CHECK(nosv_mutex_unlock(mutex));
}

void task_run(nosv_task_t task)
{
	atomic_fetch_add_explicit(&track_init, 1, memory_order_relaxed);
	struct metadata *meta = nosv_get_task_metadata(task);
	struct timespec *deadline = meta->deadline;
	lock(meta->flags);
	if (deadline) {
		// If timedwait, we don't want to check for ready value, otherwise
		// we cannot wait for the tasks to wake up on their own in the test.
		if (meta->flags & PTHREAD)
			CHECK(nosv_cond_timedwait_pthread(cond, &pthread_mutex, deadline));
		else
			CHECK(nosv_cond_timedwait(cond, mutex, deadline));

		ready--;
	} else {
		while (!ready) {
			if (meta->flags & PTHREAD)
				CHECK(nosv_cond_wait_pthread(cond, &pthread_mutex));
			else
				CHECK(nosv_cond_wait(cond, mutex));
		}
		ready--;
	}
	unlock(meta->flags);
}

void task_comp(nosv_task_t task)
{
	struct metadata *meta = nosv_get_task_metadata(task);
	// Mark this task as finished
	atomic_fetch_add_explicit(&track_completed, 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&completed[meta->tid], 1, memory_order_relaxed);
}

void test_cond(const char *pref, const char *label, const cond_test_flag_t flags, const int timed_tasks, const long time_ms)
{
	char msg[255];
	sprintf(msg, "%s-%s", pref, label);
	atomic_store_explicit(&track_init, 0, memory_order_relaxed);
	atomic_store_explicit(&track_completed, 0, memory_order_relaxed);

	for (int i = 0; i < NTASKS; i++)
		atomic_store_explicit(&completed[i], 0, memory_order_relaxed);

	struct timespec deadline;
	if (timed_tasks) {
		clock_gettime(CLOCK_MONOTONIC, &deadline);
		deadline.tv_nsec += time_ms*1000000; // *1ms
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
	CHECK(nosv_cond_broadcast(cond));
	unlock(flags);

	// Submit all tasks. Each task will call nosv_cond_wait
	for (int i = 0; i < NTASKS; i++)
		CHECK(nosv_submit(tasks[i], NOSV_SUBMIT_NONE));

	// Wait until all tasks start
	while (NTASKS != atomic_load_explicit(&track_init, memory_order_relaxed))
		CHECK(nosv_yield(NOSV_YIELD_NONE));

	// Wait until all timed tasks finish
	int completed;

	if (flags & NOWAIT) {
		test_ok(&test, "%s: not waiting for timedwait tasks", msg);
	} else {
		test_check_waitfor(&test,
						timed_tasks == (completed = atomic_load_explicit(&track_completed, memory_order_relaxed)),
						TEST_TIMEOUT,
						"%s: %d timed tasks finished, expected %d (%d remain, ready = %d)", msg, completed,
						timed_tasks, NTASKS - timed_tasks, ready);
	}

	if (flags & SIGNAL) {
		// At this point, all tasks should have blocked into the scheduler
		for (int i = 1; i <= NTASKS; i++) {
			// Unblock one task
			lock(flags);
			ready++;
			CHECK(nosv_cond_signal(cond));
			unlock(flags);

			// Wait for that task to finish
			while (i > atomic_load_explicit(&track_completed, memory_order_relaxed))
				CHECK(nosv_yield(NOSV_YIELD_NONE));

			// Ensure that only one task finished
			if (timed_tasks + i < atomic_load_explicit(&track_completed, memory_order_relaxed)) {
				test_fail(&test, "%s: Tasks were not unblocked in order: expected <= %d but found %d\n",
					  msg, i, atomic_load_explicit(&track_completed, memory_order_relaxed));
				exit(1);
			}
		}
	} else { // broadcast
		lock(flags);
		ready += NTASKS;
		CHECK(nosv_cond_broadcast(cond));
		unlock(flags);
	}

	while (NTASKS != atomic_load_explicit(&track_completed, memory_order_relaxed))
		CHECK(nosv_yield(NOSV_YIELD_NONE));

	// Wait until all tasks finish
	test_check_waitfor(&test, NTASKS == (completed = atomic_load_explicit(&track_completed, memory_order_relaxed)),
					TEST_TIMEOUT, "%s: %d unlocked correctly, expected %d", msg, completed, NTASKS);

	if (ready)
		test_error(&test, "%s: ready = %d, expected 0", msg, ready);

	for (int i = 0; i < NTASKS; i++) {
		CHECK(nosv_destroy(tasks[i], NOSV_DESTROY_NONE));
	}
}

void init_objs() {
	// Prepare the global cond and mutex
	CHECK(nosv_cond_init(&cond, NOSV_COND_NONE));
	CHECK(nosv_mutex_init(&mutex, NOSV_MUTEX_NONE));
	CHECK(pthread_mutex_init(&pthread_mutex, NULL));
}

void free_objs() {
	// Free the cond object and mutex
	CHECK(nosv_cond_destroy(cond));
	CHECK(nosv_mutex_destroy(mutex));
	CHECK(pthread_mutex_destroy(&pthread_mutex));
}

#define NVARIANTS 4

int main()
{
	nosv_task_t task;

	test_init(&test, 2*7*NVARIANTS);

	// Init nosv
	CHECK(nosv_init());
	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));

	cond_test_flag_t variants[NVARIANTS] = {BROADCAST, SIGNAL, BROADCAST | PTHREAD, SIGNAL | PTHREAD};
	char *labels[NVARIANTS] = {"broadcast", "signal", "broadcast-pthread", "signal-pthread"};

	const int timeout_ms = 250;

	for (int i = 0; i < NVARIANTS; ++i) {
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

