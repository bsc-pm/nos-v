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


#define NTASKS 8
#define TEST_TIMEOUT 4000

enum cond_test_flag {
	BROADCAST    = __ZEROBITS,
	SIGNAL		 = __BIT(0), // 0 -> broadcast, 1 -> signal
	NOWAIT		 = __BIT(1), // Don't wait for timed tasks to expire
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

struct metadata {
	int tid;
	struct timespec *deadline;
};

void task_run(nosv_task_t task)
{
	atomic_fetch_add_explicit(&track_init, 1, memory_order_relaxed);
	CHECK(nosv_mutex_lock(mutex));
	struct metadata *meta = nosv_get_task_metadata(task);
	struct timespec *deadline = meta->deadline;
	if (deadline) {
		// If timedwait, we don't want to check for ready value, otherwise
		// we cannot wait for the tasks to wake up on their own in the test.
		CHECK(nosv_cond_timedwait(cond, mutex, deadline));
		ready--;
	} else {
		while (!ready) {
			CHECK(nosv_cond_wait(cond, mutex));
		}
		ready--;
	}
	CHECK(nosv_mutex_unlock(mutex));
}

void task_comp(nosv_task_t task)
{
	struct metadata *meta = nosv_get_task_metadata(task);
	// Mark this task as finished
	atomic_fetch_add_explicit(&track_completed, 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&completed[meta->tid], 1, memory_order_relaxed);
}

void test_cond(const char *msg, const cond_test_flag_t flags, const int timed_tasks, const int td)
{
	atomic_store_explicit(&track_init, 0, memory_order_relaxed);
	atomic_store_explicit(&track_completed, 0, memory_order_relaxed);

	for (int i = 0; i < NTASKS; i++)
		atomic_store_explicit(&completed[i], 0, memory_order_relaxed);

	struct timespec deadline;
	if (timed_tasks) {
		clock_gettime(CLOCK_MONOTONIC, &deadline);
		deadline.tv_nsec += td*1000000; // *1ms
	}

	for (int i = 0; i < NTASKS; i++) {
		CHECK(nosv_create(&tasks[i], task_type, sizeof(struct metadata), NOSV_CREATE_NONE));
		struct metadata *meta = nosv_get_task_metadata(tasks[i]);
		meta->tid = i;

		// Only use timedwait for the first n tasks
		meta->deadline = i < timed_tasks ? &deadline : NULL;
	}

	// broadcast into empty queue, should do nothing
	CHECK(nosv_mutex_lock(mutex));
	CHECK(nosv_cond_broadcast(cond));
	CHECK(nosv_mutex_unlock(mutex));

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
			CHECK(nosv_mutex_lock(mutex));
			ready++;
			CHECK(nosv_cond_signal(cond));
			CHECK(nosv_mutex_unlock(mutex));

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
		CHECK(nosv_mutex_lock(mutex));
		ready += NTASKS;
		CHECK(nosv_cond_broadcast(cond));
		CHECK(nosv_mutex_unlock(mutex));

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
}

void free_objs() {
	// Free the cond object and mutex
	CHECK(nosv_cond_destroy(cond));
	CHECK(nosv_mutex_destroy(mutex));
}

int main()
{
	nosv_task_t task;

	test_init(&test, 24);

	// Init nosv
	CHECK(nosv_init());
	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));

	init_objs();

	// Run the first test
	test_cond("broadcast", BROADCAST, 0, 0);
	// Repeat, this time reusing the cond
	test_cond("broadcast-reuse", BROADCAST, 0, 0);

	// All tasks with timedwait 1 second in future
	test_cond("broadcast-timed-full", BROADCAST, NTASKS, 200);
	// Half the tasks with timedwait, 1 second in future
	test_cond("broadcast-timed-half", BROADCAST, NTASKS/2, 200);
	// All tasks with timedwait, with expired deadline (100 seconds ago)
	test_cond("broadcast-timed-expired", BROADCAST, NTASKS, -1000);

	// Half the tasks with timedwait, 1 second in future
	test_cond("broadcast-timed-full-nowait", BROADCAST | NOWAIT, NTASKS, 200);

	// Reset objects for signal test
	free_objs();
	init_objs();

	// Run signal test
	test_cond("signal", SIGNAL, 0, 0);
	// Repeat, this time reusing the cond
	test_cond("signal-reuse", SIGNAL, 0, 0);

	// Signal timedwaits
	test_cond("signal-timed-full", SIGNAL, NTASKS, 200);
	// Half the tasks with timedwait, 1 second in future
	test_cond("signal-timed-half", SIGNAL, NTASKS/2, 200);
	// All tasks with timedwait, with expired deadline (100 seconds ago)
	test_cond("signal-timed-expired", SIGNAL, NTASKS, -1000);

	test_cond("signal-timed-full-nowait", SIGNAL | NOWAIT, NTASKS, 200);

	// Shutdown nosv
	free_objs();
	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());
}

