/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"

#include <nosv.h>
#include <nosv/affinity.h>
#include <sched.h>
#include <stdatomic.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define NITERATIONS 50
#define NTASKS 4

volatile int iteration = 0;
atomic_int completed = 0;

nosv_task_t tasks[NTASKS];

test_t test;

// All tasks have a strict affinity to the same core and only one can run
// simultaneously. The idea is that there is a specific number of iterations
// to perform and these will be executed by the different task in an interleaved
// way. At the end, each task will have performed NITERATIONS / NTASKS but
// following a round-robin fashion
void task_run(nosv_task_t task)
{
	const int time_us = 200;

	// Get a turn that indicates when the task is allowed to run an iteration
	const int turn = (iteration % NTASKS);

	// Increase the iterations in a interleaved way
	while (iteration < NITERATIONS) {
		// Execute an iteration since it's the turn of the current task
		++iteration;

		// Now we should yield the CPU and wait until it's our next turn
		do {
			// Yield the CPU to other ready tasks
			nosv_yield(NOSV_YIELD_NONE);

			// Consume quantum without wasting resources
			usleep(time_us);
		} while (iteration % NTASKS != turn && iteration < NITERATIONS);
	}

	test_ok(&test, "Task executed all iterations");
}

void task_comp(nosv_task_t task)
{
	atomic_fetch_add(&completed, 1);
}

int main() {
	test_init(&test, NTASKS);

	nosv_init();

	nosv_task_type_t task_type;
	nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE);

	for (int t = 0; t < NTASKS; ++t)
		nosv_create(&tasks[t], task_type, 0, NOSV_CREATE_NONE);

	// Parallel tests should be using the same CPU
	int cpu = test_get_first_cpu();
	assert(cpu >= 0);

	nosv_affinity_t affinity = nosv_affinity_get(cpu, NOSV_AFFINITY_LEVEL_CPU, NOSV_AFFINITY_TYPE_STRICT);

	for (int t = 0; t < NTASKS; ++t) {
		nosv_set_task_affinity(tasks[t], &affinity);
		nosv_submit(tasks[t], NOSV_SUBMIT_NONE);
	}

	while (atomic_load(&completed) != NTASKS)
		usleep(1000);

	for (int t = 0; t < NTASKS; ++t)
		nosv_destroy(tasks[t], NOSV_DESTROY_NONE);

	nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE);

	nosv_shutdown();

	return 0;
}
