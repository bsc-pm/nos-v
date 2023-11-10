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

test_t test;

void task_run(nosv_task_t task)
{
	atomic_store(&run, 1);
}

void task_comp(nosv_task_t task)
{
	atomic_store(&comp, 1);
}

int main() {
	test_init(&test, 2);
	test_option(&test, TEST_OPTION_PARALLEL, 0);

	CHECK(nosv_init());

	nosv_task_t task;
	nosv_task_t main_task;

	// Attach main thread
	CHECK(nosv_attach(&main_task, NULL, "main", NOSV_ATTACH_NONE));

	// Create some task type
	nosv_task_type_t task_type;
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));
	CHECK(nosv_create(&task, task_type, 0, NOSV_CREATE_NONE));
	CHECK(nosv_submit(task, NOSV_SUBMIT_IMMEDIATE));

	CHECK(nosv_detach(NOSV_DETACH_NONE));

	test_check_timeout(&test, atomic_load(&run), 20, "Immediate successor of attached task ran");
	test_check_timeout(&test, atomic_load(&comp), 20, "Immediate successor of attached task was completed");

	CHECK(nosv_destroy(task, NOSV_DESTROY_NONE));
	CHECK(nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE));

	CHECK(nosv_shutdown());

	return 0;
}
