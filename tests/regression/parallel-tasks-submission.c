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

atomic_int run;
atomic_int comp;

atomic_int run_second;
atomic_int comp_second;

#define DEGREE 16

test_t test;

nosv_task_type_t task_type_second;

void task_run(nosv_task_t task)
{
	nosv_task_t new_task;
	CHECK(nosv_create(&new_task, task_type_second, 0, NOSV_CREATE_NONE));
	nosv_set_task_degree(new_task, DEGREE);
	CHECK(nosv_submit(new_task, NOSV_SUBMIT_IMMEDIATE));
	atomic_fetch_add(&run, 1);
}

void task_comp(nosv_task_t task)
{
	atomic_fetch_add(&comp, 1);
}

void task_second_run(nosv_task_t task)
{
	atomic_fetch_add(&run_second, 1);
}

void task_second_comp(nosv_task_t task)
{
	CHECK(nosv_destroy(task, NOSV_DESTROY_NONE));
	atomic_fetch_add(&comp_second, 1);
}


int main() {
	test_init(&test, 4);

	CHECK(nosv_init());

	nosv_task_t task;
	nosv_task_t main_task;

	// Attach main thread
	nosv_task_type_t attach_type;
	CHECK(nosv_type_init(&attach_type, NULL, NULL, NULL, "main", NULL, NULL, NOSV_TYPE_INIT_EXTERNAL));
	CHECK(nosv_attach(&main_task, attach_type, 0, NULL, NOSV_ATTACH_NONE));

	// Create some task type
	nosv_task_type_t task_type;
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));
	CHECK(nosv_type_init(&task_type_second, task_second_run, NULL, task_second_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));

	CHECK(nosv_create(&task, task_type, 0, NOSV_CREATE_NONE));
	CHECK(nosv_submit(task, NOSV_SUBMIT_IMMEDIATE));

	test_check_waitfor(&test, atomic_load(&run) == 1, 20, "First task has ran once");
	test_check_waitfor(&test, atomic_load(&comp) == 1, 20, "First task has completed once");
	test_check_waitfor(&test, atomic_load(&run_second) == DEGREE, 100, "Parallel task has ran DEGREE times");
	CHECK(nosv_waitfor(10 * 1000ULL * 1000ULL, NULL));
	test_check_waitfor(&test, atomic_load(&comp_second) == 1, 20, "Parallel task has completed once");

	CHECK(nosv_detach(NOSV_DETACH_NONE));

	CHECK(nosv_destroy(task, NOSV_DESTROY_NONE));
	CHECK(nosv_type_destroy(attach_type, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_type_destroy(task_type_second, NOSV_TYPE_DESTROY_NONE));

	CHECK(nosv_shutdown());

	return 0;
}
