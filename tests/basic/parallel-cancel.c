/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"

#include <nosv.h>
#include <nosv/affinity.h>
#include <stdatomic.h>
#include <unistd.h>

#define NTASKS 200

int degree;

atomic_int nr_completed_tasks;
atomic_int task_cnt[NTASKS];

nosv_task_t tasks[NTASKS];
nosv_task_t main_task;

test_t test;

void task_run(nosv_task_t task)
{
	atomic_int *m = nosv_get_task_metadata(task);
	atomic_fetch_add(m, 1);
	nosv_cancel(NOSV_CANCEL_NONE);
}

void task_comp(nosv_task_t task)
{
	atomic_int *m = nosv_get_task_metadata(task);
	int res = atomic_load(m);
	test_check(&test, res < degree, "Task has been executed less than %d times (actual: %d)", degree, res);

	int total = atomic_fetch_add(&nr_completed_tasks, 1);
	if (total == NTASKS - 1) {
		nosv_submit(main_task, NOSV_SUBMIT_UNLOCKED);
	}
}

int main() {
	test_init(&test, 1 + NTASKS);

	nosv_init();

	nosv_task_type_t attach_type;
	nosv_type_init(&attach_type, NULL, NULL, NULL, "main", NULL, NULL, NOSV_TYPE_INIT_EXTERNAL);
	nosv_attach(&main_task, attach_type, 0, NULL, NOSV_ATTACH_NONE);

	nosv_task_type_t task_type;
	nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE);

	// Double the number of CPUs in degree to guarantee that we cannot execute everything in parallel
	degree = nosv_get_num_cpus() * 2;

	// The submitted tasks will execute after we yield, since they also are on CPU 0
	for (int t = 0; t < NTASKS; ++t) {
		nosv_create(&tasks[t], task_type, sizeof(atomic_int), NOSV_CREATE_NONE);
		atomic_int *m = nosv_get_task_metadata(tasks[t]);
		atomic_store(m, 0);
		nosv_set_task_degree(tasks[t], degree);
		nosv_submit(tasks[t], NOSV_SUBMIT_NONE);
	}

	nosv_pause(NOSV_PAUSE_NONE);

	test_check(&test, atomic_load(&nr_completed_tasks) == (NTASKS), "Could run all tasks with parallel execution");

	for (int t = 0; t < NTASKS; ++t)
		nosv_destroy(tasks[t], NOSV_DESTROY_NONE);

	nosv_detach(NOSV_DETACH_NONE);

	nosv_type_destroy(attach_type, NOSV_TYPE_DESTROY_NONE);
	nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE);

	nosv_shutdown();

	return 0;
}
