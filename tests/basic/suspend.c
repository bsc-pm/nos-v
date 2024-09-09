/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"
#include "common/utils.h"

#include <nosv.h>
#include <nosv/affinity.h>
#include <sched.h>
#include <stdatomic.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

test_t test;

atomic_int completed = 0;

void task_comp(nosv_task_t task)
{
	test_ok(&test, "Task completeed correctly");
	atomic_fetch_add(&completed, 1);
}

void test_wrapper(nosv_task_run_callback_t run)
{
	nosv_task_t t;
	nosv_task_type_t tt;
	CHECK(nosv_type_init(&tt, run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));
	CHECK(nosv_create(&t, tt, 0, NOSV_CREATE_NONE));
	CHECK(nosv_submit(t, NOSV_SUBMIT_NONE));

	test_check_timeout(&test, atomic_load(&completed) == 1, 1000, "Tasks are completed");
	atomic_store(&completed, 0);

	CHECK(nosv_destroy(t, NOSV_DESTROY_NONE));
	CHECK(nosv_type_destroy(tt, NOSV_TYPE_DESTROY_NONE));
}

// None
atomic_int val_1 = 0;
void test_1(nosv_task_t task)
{
	switch (atomic_load(&val_1)) {
		case 0:
			CHECK(nosv_set_suspend_mode(NOSV_SUSPEND_MODE_NONE, 0));
			CHECK(nosv_suspend());
			atomic_fetch_add(&val_1, 1);
			// This will not complete
			atomic_fetch_add(&completed, 1);
			return;
		case 1:
			test_fail(&test, "Task didn't complete");
	}
}


// Resubmit
atomic_int val_2 = 0;
void test_2(nosv_task_t task)
{
	switch (atomic_load(&val_2)) {
		case 0:
			CHECK(nosv_set_suspend_mode(NOSV_SUSPEND_MODE_TIMEOUT_SUBMIT, 1000));
			CHECK(nosv_suspend());
			atomic_fetch_add(&val_2, 1);
			return;
		case 1:
			test_ok(&test, "Task resumed correctly");
	}
}

// Wait events (no wait)
atomic_int val_3 = 0;
void test_3(nosv_task_t task)
{
	switch (atomic_load(&val_3)) {
		case 0:
			CHECK(nosv_set_suspend_mode(NOSV_SUSPEND_MODE_EVENT_SUBMIT, 0));
			CHECK(nosv_suspend());
			atomic_fetch_add(&val_3, 1);
			return;
		case 1:
			test_ok(&test, "Task resumed correctly");
	}
}

// Wait events (wait)
nosv_task_t subtask_4;
nosv_task_type_t subtask_type_4;
atomic_uint_fast64_t task_4;

void subtest_run_4(nosv_task_t task)
{
}

void subtest_4(nosv_task_t task)
{
	nosv_task_t t = (nosv_task_t) atomic_load(&task_4);	
	usleep(1000);
	CHECK(nosv_decrease_event_counter(t, 1));
}

atomic_int val_4 = 0;
void test_4(nosv_task_t task)
{
	switch (atomic_load(&val_4)) {
		case 0:
			CHECK(nosv_increase_event_counter(1));
			atomic_store(&task_4, (uint64_t) task);

			CHECK(nosv_type_init(&subtask_type_4, subtest_run_4, NULL, subtest_4, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));
			CHECK(nosv_create(&subtask_4, subtask_type_4, 0, NOSV_CREATE_NONE));
			CHECK(nosv_submit(subtask_4, NOSV_SUBMIT_NONE));

			CHECK(nosv_set_suspend_mode(NOSV_SUSPEND_MODE_EVENT_SUBMIT, 0));
			CHECK(nosv_suspend());
			atomic_fetch_add(&val_4, 1);
			return;
		case 1:
			test_ok(&test, "Task resumed correctly");
			CHECK(nosv_destroy(subtask_4, NOSV_DESTROY_NONE));
			CHECK(nosv_type_destroy(subtask_type_4, NOSV_TYPE_DESTROY_NONE));
	}
}

int main() {
	test_init(&test, 1 + 3 + 3 + 3);

	CHECK(nosv_init());

	//Task 1
	test_wrapper(test_1);
	//Task 2
	test_wrapper(test_2);
	//Task 3
	test_wrapper(test_3);
	//Task 4
	test_wrapper(test_4);

	CHECK(nosv_shutdown());

	return 0;
}
