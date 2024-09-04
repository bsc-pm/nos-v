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

#define NTASKS 128


test_t test;
nosv_task_type_t task_type;
nosv_task_t tasks[NTASKS];
atomic_uint track_init;
atomic_uint track_completed;


void task_run(nosv_task_t task)
{
	// Mark this task as running
	atomic_fetch_add_explicit(&track_init, 1, memory_order_relaxed);

	int *npauses = nosv_get_task_metadata(task);
	// Block
	for (int i = 0; i < *npauses; ++i) {
		CHECK(nosv_pause(NOSV_PAUSE_NONE));
	}
}

void task_comp(nosv_task_t task)
{
	// Mark this task as finished
	atomic_fetch_add_explicit(&track_completed, 1, memory_order_relaxed);
}

void pause_test(unsigned int npauses)
{
	atomic_store_explicit(&track_init, 0, memory_order_relaxed);
	atomic_store_explicit(&track_completed, 0, memory_order_relaxed);

	for (int i = 0; i < NTASKS; i++) {
		CHECK(nosv_create(&tasks[i], task_type, sizeof(unsigned int), NOSV_CREATE_NONE));
		unsigned int *task_npauses = nosv_get_task_metadata(tasks[i]);
		*task_npauses = npauses;
	}

	// Submit all tasks. Each task will block on the first nosv_pause
	for (int i = 0; i < NTASKS; i++)
		CHECK(nosv_submit(tasks[i], NOSV_SUBMIT_NONE));

	// Wait for all tasks to have a chance to run
	while (atomic_load_explicit(&track_init, memory_order_relaxed) != NTASKS)
		CHECK(nosv_yield(NOSV_YIELD_NONE));

	if (atomic_load_explicit(&track_completed, memory_order_relaxed)) {
		test_fail(&test, "%u: All tasks were unlocked correctly", npauses);
		exit(1);
	}

	// Re-submit all tasks as many times as there are pauses
	for (int i = 0; i < NTASKS; i++) {
		for (int j = 0; j < npauses; j++) {
			CHECK(nosv_submit(tasks[i], NOSV_SUBMIT_NONE));
		}
	}

	// Wait for all tasks to have a chance to finish
	test_check_timeout(&test, atomic_load_explicit(&track_completed, memory_order_relaxed) == NTASKS, 2000,
					   "%u: All tasks were unlocked correctly", npauses);

	for (int i = 0; i < NTASKS; i++)
		CHECK(nosv_destroy(tasks[i], NOSV_DESTROY_NONE));
}

int main()
{
	nosv_task_t task;

	const unsigned int npauses[] = {2, 8, 32};
	const size_t n = sizeof(npauses)/sizeof(unsigned int);

	test_init(&test, n);

	// Init nosv
	CHECK(nosv_init());
	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));

	for (int i = 0; i < n; i++)
		pause_test(npauses[i]);

	// Shutdown nosv
	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());

	return 0;
}
