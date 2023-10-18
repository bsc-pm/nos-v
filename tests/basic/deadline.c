/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"
#include "common/utils.h"

#include <nosv.h>
#include <nosv/affinity.h>
#include <stdatomic.h>
#include <unistd.h>

#define NTASKS 4

volatile int completed[NTASKS];
atomic_int nr_completed_tasks;

nosv_task_t tasks[NTASKS];

test_t test;

volatile int iteration = 0;

// We set strict affinity to the same core to ensure that tasks are not run simultaneously.
// Then, upon running, we set a deadline for each task. In normal conditions, tasks should
// be run in the order that is imposed by their deadlines.
void task_run(nosv_task_t task)
{
	const uint64_t time_ns = 500ULL * 1000ULL * 1000ULL;

	// Get the index of this task
	const int id = (iteration++ % NTASKS);

	uint64_t actual_ns = 0;
	CHECK(nosv_waitfor(time_ns * (id + 1), &actual_ns));

	// Allow a 10% deviation over the real time.
	uint64_t expected = time_ns * (id + 1);
	uint64_t difference = (actual_ns > expected) ? actual_ns - expected : expected - actual_ns;

	test_check(&test, difference < expected/10, "Actual time has a deviation of less than 10\% (got %.2f\%)",
		((float)difference / (float)expected) * 100.0);

	int fail = 0;
	// Then verify the order
	for (int i = 0; i < id; ++i)
		if (!completed[i])
			fail = 1;

	completed[id] = 1;

	test_check(&test, !fail, "Task executed in correct order based on deadlines");
}

void task_comp(nosv_task_t task)
{
	atomic_fetch_add(&nr_completed_tasks, 1);
}

int main() {
	test_init(&test, NTASKS * 2);

	CHECK(nosv_init());

	nosv_task_type_t task_type;
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));

	for (int t = 0; t < NTASKS; ++t)
		CHECK(nosv_create(&tasks[t], task_type, 0, NOSV_CREATE_NONE));

	// Parallel tests should be using the same CPU
	int cpu = test_get_first_cpu();
	assert(cpu >= 0);

	nosv_affinity_t affinity = nosv_affinity_get(cpu, NOSV_AFFINITY_LEVEL_CPU, NOSV_AFFINITY_TYPE_STRICT);

	for (int t = 0; t < NTASKS; ++t) {
		nosv_set_task_affinity(tasks[t], &affinity);
		CHECK(nosv_submit(tasks[t], NOSV_SUBMIT_NONE));
	}

	while (atomic_load(&nr_completed_tasks) != NTASKS)
		usleep(1000);

	for (int t = 0; t < NTASKS; ++t)
		CHECK(nosv_destroy(tasks[t], NOSV_DESTROY_NONE));

	CHECK(nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE));

	CHECK(nosv_shutdown());

	return 0;
}
