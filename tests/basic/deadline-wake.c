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

#define NTASKS 400

atomic_int nr_completed_tasks;
test_t test;
nosv_task_t tasks[NTASKS];


void task_run(nosv_task_t task)
{
	const uint64_t time_ns = 60ULL * 1000ULL * 1000ULL * 1000ULL;
	CHECK(nosv_waitfor(time_ns, NULL));
}

void task_comp(nosv_task_t task)
{
	atomic_fetch_add(&nr_completed_tasks, 1);
}

int wait_for_tasks(int ntasks, size_t deadline, size_t start_s)
{
	size_t elapsed_s;
	struct timespec end;

	while (atomic_load(&nr_completed_tasks) != ntasks) {
		usleep(1000);
		clock_gettime(CLOCK_MONOTONIC, &end);
		elapsed_s = end.tv_sec - start_s;

		if (elapsed_s >= deadline)
			return 1;
	}

	return 0;
}

int main()
{
	struct timespec start;
	nosv_task_type_t task_type;

	// We only report one test because if any of the tests run here fails, a
	// task might end up dangling into the scheduler which might affect the
	// results of the other tests. Therefore, we abort as soon as a test
	// fails.
	test_init(&test, 1);

	CHECK(nosv_init());
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));


	// TEST1: Wake a task even before it has been scheduled
	atomic_init(&nr_completed_tasks, 0);
	CHECK(nosv_create(&tasks[0], task_type, 0, NOSV_CREATE_NONE));
	clock_gettime(CLOCK_MONOTONIC, &start);
	CHECK(nosv_submit(tasks[0], NOSV_SUBMIT_DEADLINE_WAKE));
	CHECK(nosv_submit(tasks[0], NOSV_SUBMIT_NONE));
	if (wait_for_tasks(1, 5, start.tv_sec)) {
		test_check(&test, 0, "Waiting for a waitfor task that was woken up before it was scheduled");
		return 1;
	}
	CHECK(nosv_destroy(tasks[0], NOSV_DESTROY_NONE));

	// TEST2: Wake a task after it is waiting in the red-black tree
	atomic_init(&nr_completed_tasks, 0);
	CHECK(nosv_create(&tasks[0], task_type, 0, NOSV_CREATE_NONE));
	CHECK(nosv_submit(tasks[0], NOSV_SUBMIT_NONE));
	usleep(200000);
	clock_gettime(CLOCK_MONOTONIC, &start);
	CHECK(nosv_submit(tasks[0], NOSV_SUBMIT_DEADLINE_WAKE));
	if (wait_for_tasks(1, 5, start.tv_sec)) {
		test_check(&test, 0, "Waiting for a waitfor task that was woken up while waiting in the red-black tree");
		return 1;
	}
	CHECK(nosv_destroy(tasks[0], NOSV_DESTROY_NONE));

	// TEST3: Wake tasks at random points of its life cycle
	atomic_init(&nr_completed_tasks, 0);
	for (int t = 0; t < NTASKS; t++)
		CHECK(nosv_create(&tasks[t], task_type, 0, NOSV_CREATE_NONE));
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (int t = 0; t < NTASKS; t++)
		CHECK(nosv_submit(tasks[t], NOSV_SUBMIT_NONE));
	for (int t = 0; t < NTASKS; t++)
		CHECK(nosv_submit(tasks[t], NOSV_SUBMIT_DEADLINE_WAKE));
	if (wait_for_tasks(NTASKS, 5, start.tv_sec)) {
		test_check(&test, 0, "Waiting for waitfor tasks that were awoken after they were scheduled");
		return 1;
	}
	for (int t = 0; t < NTASKS; t++)
		CHECK(nosv_destroy(tasks[t], NOSV_DESTROY_NONE));

	// Cleanup
	CHECK(nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_shutdown());

	test_check(&test, 1, "Waiting for a waitfor tasks to wake up before its timeout expires");

	return 0;
}

