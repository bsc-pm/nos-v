/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#include <nosv.h>
#include <nosv/affinity.h>
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
atomic_uint track_mid;
atomic_uint track_completed;
nosv_mutex_t mutex;

atomic_uint affinity_ok = 0;

void task_run(nosv_task_t task)
{
	// Mark this task as running
	atomic_fetch_add_explicit(&track_init, 1, memory_order_relaxed);
	// Block
	CHECK(nosv_mutex_lock(mutex));

	nosv_affinity_t aff = nosv_get_task_affinity(nosv_self());
	if (aff.level == NOSV_AFFINITY_LEVEL_CPU &&
			aff.type == NOSV_AFFINITY_TYPE_STRICT &&
			nosv_get_current_system_cpu() == aff.index) {
		atomic_fetch_add_explicit(&affinity_ok, 1, memory_order_relaxed);
	}

	atomic_fetch_add_explicit(&track_mid, 1, memory_order_relaxed);

	CHECK(nosv_pause(NOSV_PAUSE_NONE));
}

void task_comp(nosv_task_t task)
{
	// Mark this task as finished
	atomic_fetch_add_explicit(&track_completed, 1, memory_order_relaxed);
}

void mutex_test(const char *msg, char trylock, char affinity)
{
	atomic_store_explicit(&track_init, 0, memory_order_relaxed);
	atomic_store_explicit(&track_mid, 0, memory_order_relaxed);
	atomic_store_explicit(&track_completed, 0, memory_order_relaxed);
	atomic_store_explicit(&affinity_ok, 0, memory_order_relaxed);

	for (int i = 0; i < NTASKS; i++)
		CHECK(nosv_create(&tasks[i], task_type, 0, NOSV_CREATE_NONE));

	// Prepare the global mutex
	CHECK(nosv_mutex_init(&mutex, NOSV_MUTEX_NONE));

	if (!trylock)
		CHECK(nosv_mutex_lock(mutex));
	else
		CHECK(nosv_mutex_trylock(mutex));

	// Number of available CPUs
	int cpus = get_cpus();
	// Array containing all the system CPU ids
	int *cpu_indexes = get_cpu_array();

	// Submit all tasks. Each task will block until the main thread unblocks
	// them one by one
	for (int i = 0; i < NTASKS; i++) {
		if (affinity) {
			// Get system CPU id
			int cpu = cpu_indexes[i % cpus];

			nosv_affinity_t aff = nosv_affinity_get(cpu, NOSV_AFFINITY_LEVEL_CPU, NOSV_AFFINITY_TYPE_STRICT);
			nosv_set_task_affinity(tasks[i], &aff);
		}

		CHECK(nosv_submit(tasks[i], NOSV_SUBMIT_NONE));
	}

	free(cpu_indexes);

	// Wait for all tasks to have a chance to run
	while (atomic_load_explicit(&track_init, memory_order_relaxed) != NTASKS)
		CHECK(nosv_yield(NOSV_YIELD_NONE));

	// At this point, all tasks should have blocked into the scheduler
	for (int i = 1; i <= NTASKS; i++) {
		// Unblock one task
		CHECK(nosv_mutex_unlock(mutex));

		// Wait for that task to finish
		while (i > atomic_load_explicit(&track_mid, memory_order_relaxed))
			CHECK(nosv_yield(NOSV_YIELD_NONE));

		// Ensure that only one task finished
		if (i < atomic_load_explicit(&track_mid, memory_order_relaxed)) {
			test_fail(&test, "%s: Tasks were not unblocked in order: expected <= %d but found %d\n",
				  msg, i, atomic_load_explicit(&track_mid, memory_order_relaxed));
			exit(1);
		}
	}

	// Final unlock
	CHECK(nosv_mutex_unlock(mutex));

	// Ensure that no task has finished yet
	if (atomic_load_explicit(&track_completed, memory_order_relaxed)) {
		test_error(&test, "%s: %d tasks finished (expected 0)", msg, atomic_load_explicit(&track_completed, memory_order_relaxed));
		exit(1);
	}

	// Re-submit all tasks. They should resume from the nosv_pause
	for (int i = 0; i < NTASKS; i++)
		CHECK(nosv_submit(tasks[i], NOSV_SUBMIT_NONE));

	// Wait for all tasks to have a chance to run
	test_check_waitfor(&test, atomic_load_explicit(&track_completed, memory_order_relaxed) == NTASKS, 10000,
					   "%s: All tasks were unlocked correctly", msg);

	if (affinity && atomic_load_explicit(&affinity_ok, memory_order_relaxed) != NTASKS) {
		test_error(&test, "%s: tasks did not run on their affine cpus", msg);
	}

	for (int i = 0; i < NTASKS; i++)
		CHECK(nosv_destroy(tasks[i], NOSV_DESTROY_NONE));

	// Free mutex object
	CHECK(nosv_mutex_destroy(mutex));
}

void trylock_test(const char *msg)
{
	// Prepare the global mutex
	CHECK(nosv_mutex_init(&mutex, NOSV_MUTEX_NONE));

	// Check trylock when the lock is not taken
	if (NOSV_SUCCESS != nosv_mutex_trylock(mutex)) {
		test_fail(&test, "%s: trylock returned \"taken\" when not taken", msg);
		exit(1);
	}

	// Check trylock when the lock is taken
	if (NOSV_ERR_BUSY != nosv_mutex_trylock(mutex)) {
		test_fail(&test, "%s: trylock returned \"not taken\" when lock was taken", msg);
		exit(1);
	}

	// Unlock the trylock-taken mutex
	CHECK(nosv_mutex_unlock(mutex));

	// Free the mutex
	CHECK(nosv_mutex_destroy(mutex));

	test_ok(&test, "%s: Trylock succeeds", msg);
}

int main()
{
	nosv_task_t task;

	test_init(&test, 5);

	// Init nosv
	CHECK(nosv_init());
	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));

	// Run mutex tests
	mutex_test("new", 0, 0);
	// Repeat (reuse mutex after reinit)
	mutex_test("reuse", 0, 0);
	// Run with affinity
	mutex_test("affinity", 0, 1);

	// Basic trylock test
	trylock_test("trylock");
	// Repeat, mutex test, but now with initial lock taken with trylock
	mutex_test("trylock_reuse", 1, 0);

	// Shutdown nosv
	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());

	return 0;
}
