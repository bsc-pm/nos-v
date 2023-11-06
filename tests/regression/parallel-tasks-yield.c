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
atomic_int comp_yield;

#define DEGREE 32

test_t test;

nosv_task_type_t task_type_second;

void task_run(nosv_task_t task)
{
	// Use up some time
	usleep(1000);
	atomic_fetch_add(&run_value, (int) nosv_get_execution_id());
}

void task_comp(nosv_task_t task)
{
	CHECK(nosv_destroy(task, NOSV_DESTROY_NONE));
	atomic_fetch_add(&comp, 1);
}

void task_run_yield(nosv_task_t task)
{
	while (!atomic_load(&comp)) {
		nosv_yield(NOSV_YIELD_NONE);
	}
}

void task_comp_yield(nosv_task_t task)
{
	CHECK(nosv_destroy(task, NOSV_DESTROY_NONE));
	atomic_fetch_add(&comp_yield, 1);
}

int main() {
	test_init(&test, 3);

	CHECK(nosv_init());

	nosv_task_t task, task_yield, main_task;

	// Attach main thread
	nosv_task_type_t attach_type, task_type, task_type_yield;
	CHECK(nosv_type_init(&attach_type, NULL, NULL, NULL, "main", NULL, NULL, NOSV_TYPE_INIT_EXTERNAL));
	CHECK(nosv_attach(&main_task, attach_type, 0, NULL, NOSV_ATTACH_NONE));

	// Create some task type
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));
	CHECK(nosv_type_init(&task_type_yield, task_run_yield, NULL, task_comp_yield, "task-yield", NULL, NULL, NOSV_TYPE_INIT_NONE));

	CHECK(nosv_create(&task_yield, task_type_yield, 0, NOSV_CREATE_NONE));
	CHECK(nosv_submit(task_yield, NOSV_SUBMIT_NONE));

	CHECK(nosv_create(&task, task_type, 0, NOSV_CREATE_NONE));
	int degree = nosv_get_num_cpus() * 4;
	nosv_set_task_degree(task, degree);
	CHECK(nosv_submit(task, NOSV_SUBMIT_NONE));
	CHECK(nosv_detach(NOSV_DETACH_NONE));

	// Detaching before the timeout wait allows this to happen in parallel to nOS-V working on the other CPUs
	test_check_timeout(&test, atomic_load(&comp) == 1, 2000, "Tasks are completed");

	// Calculate sum of execution ids based on the degree, take into account they begin on 0.
	int expected_sum_of_execution_id = ((degree - 1) * degree) / 2;
	test_check_timeout(&test, atomic_load(&run_value) == expected_sum_of_execution_id, 2000, "Execution ids were correct");

	// Wait for yield task before cleaning up
	test_check_timeout(&test, atomic_load(&comp_yield) == 1, 1000, "Yield task finished");

	CHECK(nosv_type_destroy(attach_type, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_type_destroy(task_type_yield, NOSV_TYPE_DESTROY_NONE));

	CHECK(nosv_shutdown());

	return 0;
}
