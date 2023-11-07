/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"
#include "common/utils.h"

#include <nosv.h>
#include <nosv/affinity.h>
#include <stdatomic.h>
#include <unistd.h>

atomic_int run_value;
atomic_int comp;

#define DEGREE 32

test_t test;

nosv_task_type_t task_type_second;

void task_run(nosv_task_t task)
{
	// Use quantum and force process change
	usleep(20*1000);
	atomic_fetch_add(&run_value, nosv_get_execution_id());
}

void task_comp(nosv_task_t task)
{
	CHECK(nosv_destroy(task, NOSV_DESTROY_NONE));
	atomic_fetch_add(&comp, 1);
}

int main() {
	test_init(&test, 2);

	CHECK(nosv_init());

	nosv_task_t task;
	nosv_task_t main_task;

	// Attach main thread
	CHECK(nosv_attach(&main_task, NULL, "main", NOSV_ATTACH_NONE));

	// Create some task type
	nosv_task_type_t task_type;
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));
	CHECK(nosv_create(&task, task_type, 0, NOSV_CREATE_NONE));
	int32_t degree = nosv_get_num_cpus();
	nosv_set_task_degree(task, degree);
	CHECK(nosv_submit(task, NOSV_SUBMIT_NONE));
	CHECK(nosv_detach(NOSV_DETACH_NONE));

	// Detaching before the timeout wait allows this to happen in parallel to nOS-V working on the other CPUs
	test_check_timeout(&test, atomic_load(&comp) == 1, 2000, "Tasks are completed");

	// Calculate sum of execution ids based on the degree, take into account they begin on 0.
	int32_t expected_sum_of_execution_id = ((degree - 1) * degree) / 2;
	test_check_timeout(&test, atomic_load(&run_value) == expected_sum_of_execution_id, 2000, "Execution ids were correct");

	CHECK(nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE));

	CHECK(nosv_shutdown());

	return 0;
}
