/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023-2024 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"
#include "common/utils.h"

#include <nosv.h>
#include <nosv/affinity.h>
#include <nosv/hwinfo.h>
#include <stdatomic.h>
#include <unistd.h>

#define NTASKS 200

int32_t degree;

atomic_int nr_completed_tasks;
atomic_int task_cnt[NTASKS];

nosv_task_t tasks[NTASKS];
nosv_task_t main_task;

test_t test;

void task_run(nosv_task_t task)
{
	atomic_int *m = nosv_get_task_metadata(task);
	atomic_fetch_add(m, 1);

	// The cancel operation can fail if the parallel task was already cancelled
	// by another task degree execution
	nosv_cancel(NOSV_CANCEL_NONE);
}

void task_comp(nosv_task_t task)
{
	atomic_int *m = nosv_get_task_metadata(task);
	int res = atomic_load(m);
	test_check(&test, res < degree, "Task has been executed less than %d times (actual: %d)", degree, res);

	int total = atomic_fetch_add(&nr_completed_tasks, 1);
	if (total == NTASKS - 1) {
		CHECK(nosv_submit(main_task, NOSV_SUBMIT_UNLOCKED));
	}
}

int main() {
	test_init(&test, 1 + NTASKS);

	CHECK(nosv_init());

	CHECK(nosv_attach(&main_task, NULL, "main", NOSV_ATTACH_NONE));

	nosv_task_type_t task_type;
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));

	// Double the number of CPUs in degree to guarantee that we cannot execute everything in parallel
	// Then, +1 because the "cancel" works on the *next* iteration, so it is theoretically possible we execute
	// tasks exactly twice (and will surely happen with NCPUS=1!)
	degree = nosv_get_num_cpus() * 2 + 1;

	// The submitted tasks will execute after we yield, since they also are on CPU 0
	for (int t = 0; t < NTASKS; ++t) {
		CHECK(nosv_create(&tasks[t], task_type, sizeof(atomic_int), NOSV_CREATE_PARALLEL));
		atomic_int *m = nosv_get_task_metadata(tasks[t]);
		atomic_store(m, 0);
		nosv_set_task_degree(tasks[t], degree);
		CHECK(nosv_submit(tasks[t], NOSV_SUBMIT_NONE));
	}

	CHECK(nosv_pause(NOSV_PAUSE_NONE));

	test_check(&test, atomic_load(&nr_completed_tasks) == (NTASKS), "Could run all tasks with parallel execution");

	for (int t = 0; t < NTASKS; ++t)
		CHECK(nosv_destroy(tasks[t], NOSV_DESTROY_NONE));

	CHECK(nosv_detach(NOSV_DETACH_NONE));

	CHECK(nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE));

	CHECK(nosv_shutdown());

	return 0;
}
