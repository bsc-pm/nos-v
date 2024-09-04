/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#include <nosv.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#include "test.h"
#include "common/utils.h"


#define NTASKS 8

test_t test;
nosv_task_type_t task_type;
nosv_task_t tasks[NTASKS];
atomic_uint track_init;
atomic_uint track_mid;
atomic_uint track_completed;
nosv_barrier_t barrier;


void task_run(nosv_task_t task)
{
	atomic_fetch_add_explicit(&track_init, 1, memory_order_relaxed);
	CHECK(nosv_barrier_wait(barrier));

	atomic_fetch_add_explicit(&track_mid, 1, memory_order_relaxed);
	CHECK(nosv_pause(NOSV_PAUSE_NONE));
}

void task_comp(nosv_task_t task)
{
	// Mark this task as finished
	atomic_fetch_add_explicit(&track_completed, 1, memory_order_relaxed);
}

void test_barrier(const char *msg)
{
	atomic_store_explicit(&track_init, 0, memory_order_relaxed);
	atomic_store_explicit(&track_mid, 0, memory_order_relaxed);
	atomic_store_explicit(&track_completed, 0, memory_order_relaxed);

	for (int i = 0; i < NTASKS; i++)
		CHECK(nosv_create(&tasks[i], task_type, 0, NOSV_CREATE_NONE));

	// Submit all tasks. Each task will call nosv_barrier_wait
	for (int i = 0; i < NTASKS; i++)
		CHECK(nosv_submit(tasks[i], NOSV_SUBMIT_NONE));

	// Wait until all tasks start
	while (NTASKS != atomic_load_explicit(&track_init, memory_order_relaxed))
		CHECK(nosv_yield(NOSV_YIELD_NONE));

	// Ensure that no task has finished yet
	if (atomic_load_explicit(&track_mid, memory_order_relaxed)) {
		test_error(&test, "%s: %d tasks crossed barrier (expected 0)", msg, atomic_load_explicit(&track_mid, memory_order_relaxed));
		exit(1);
	}

	// Block until all task reach the barrier
	CHECK(nosv_barrier_wait(barrier));

	// Wait until all tasks finish first barrier
	test_check_waitfor(&test, atomic_load_explicit(&track_mid, memory_order_relaxed) == NTASKS, 10000,
			"%s: All tasks were unlocked correctly from barrier", msg);

	// Ensure that no task has finished yet
	if (atomic_load_explicit(&track_completed, memory_order_relaxed)) {
		test_error(&test, "%s: %d tasks finished (expected 0)", msg, atomic_load_explicit(&track_completed, memory_order_relaxed));
		exit(1);
	}

	// Re-submit all tasks. Each task should wake from the nosv_pause
	for (int i = 0; i < NTASKS; i++)
		CHECK(nosv_submit(tasks[i], NOSV_SUBMIT_NONE));

	// Wait until all tasks finish
	test_check_waitfor(&test, atomic_load_explicit(&track_completed, memory_order_relaxed) == NTASKS, 10000,
			"%s: All tasks were unlocked correctly from pause", msg);

	for (int i = 0; i < NTASKS; i++)
		CHECK(nosv_destroy(tasks[i], NOSV_DESTROY_NONE));
}

int main()
{
	nosv_task_t task;

	test_init(&test, 4);

	// Init nosv
	CHECK(nosv_init());
	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));

	// Prepare the global barrier
	CHECK(nosv_barrier_init(&barrier, NOSV_BARRIER_NONE, NTASKS + 1));

	// Run the first test
	test_barrier("new");

	// Repeat, this time reusing the barrier
	test_barrier("reuse");

	// Free the barrier object
	CHECK(nosv_barrier_destroy(barrier));

	// Shutdown nosv
	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());
}

