/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"
#include "common/utils.h"

#include <nosv.h>
#include <stdatomic.h>
#include <unistd.h>

#define NTASKS 8

nosv_task_t task_attach;

atomic_int started_tasks = 0;

nosv_task_type_t task_type;
nosv_task_t tasks[NTASKS];

test_t test;


void task_run(nosv_task_t task)
{
	atomic_fetch_add(&started_tasks, 1);
}

void task_comp(nosv_task_t task)
{
}

// The default submit window size is 1, therefore it should submit (and execute) the task directly
void test1()
{
	nosv_task_t task;
	CHECK(nosv_create(&task, task_type, 0, NOSV_CREATE_NONE));
	CHECK(nosv_submit(task, NOSV_SUBMIT_NONE));
	//Wait 
	int c = 0;
	while ((atomic_load(&started_tasks) != 1) && (c++ < 100))
		usleep(10000);
	//Check 1
	test_check(&test, atomic_load(&started_tasks) == 1, "Could submit the task (Default)");
	CHECK(nosv_destroy(task, NOSV_DESTROY_NONE));
}

// When the submit window size (maximum size) is bigger than 1, the tasks are added to the submit window 
// and are only submitted when the window reaches the maximum size, then all the tasks are submitted
void test2()
{
	CHECK(nosv_set_submit_window_size(NTASKS));

	for (int t = 0; t < NTASKS-1; ++t) {
		CHECK(nosv_create(&tasks[t], task_type, 0, NOSV_CREATE_NONE));
		CHECK(nosv_submit(tasks[t], NOSV_SUBMIT_NONE));
	}
	// Wait
	usleep(10000);
	// Check 0
	test_check(&test, atomic_load(&started_tasks) == 0, "Added %d tasks to the submit window only", NTASKS-1);
	// Submit last
	CHECK(nosv_create(&tasks[NTASKS-1], task_type, 0, NOSV_CREATE_NONE));
	CHECK(nosv_submit(tasks[NTASKS-1], NOSV_SUBMIT_NONE));
	// Wait
	int c = 0;
	while ((atomic_load(&started_tasks) != NTASKS) && (c++ < 100))
		usleep(10000);
	// Check NTASKS
	test_check(&test, atomic_load(&started_tasks) == NTASKS, "Flushed the submit window after submitting one more");

	for (int t = 0; t < NTASKS; ++t)
		CHECK(nosv_destroy(tasks[t], NOSV_DESTROY_NONE));
}

// Alternatively, the window can be flushed anytime using nosv_flush_submit_window()
void test3()
{
	// Already set in the previous test
	CHECK(nosv_set_submit_window_size(NTASKS));

	for (int t = 0; t < NTASKS-1; ++t) {
		CHECK(nosv_create(&tasks[t], task_type, 0, NOSV_CREATE_NONE));
		CHECK(nosv_submit(tasks[t], NOSV_SUBMIT_NONE));
	}
	// Wait
	usleep(10000);
	// Check 0
	test_check(&test, atomic_load(&started_tasks) == 0, "Added %d tasks to the submit window only", NTASKS-1);

	//Flush Window
	CHECK(nosv_flush_submit_window());
	// Wait
	int c = 0;
	while ((atomic_load(&started_tasks) != NTASKS-1) && (c++ < 100))
		usleep(10000);
	// Check NTASKS-1
	test_check(&test, atomic_load(&started_tasks) == NTASKS-1, "Flushed the submit window explicitly");

	for (int t = 0; t < NTASKS-1; ++t)
		CHECK(nosv_destroy(tasks[t], NOSV_DESTROY_NONE));
	
}

// The submit window never reaches the maximum size, therefore there is no flush.
// Until we stop incrementing the submit window size.
void test4()
{
	CHECK(nosv_set_submit_window_size(2));
	CHECK(nosv_create(&tasks[0], task_type, 0, NOSV_CREATE_NONE));
	CHECK(nosv_submit(tasks[0], NOSV_SUBMIT_NONE));

	for (int t = 1; t < NTASKS-1; ++t) {
		CHECK(nosv_set_submit_window_size(t+2));
		CHECK(nosv_create(&tasks[t], task_type, 0, NOSV_CREATE_NONE));
		CHECK(nosv_submit(tasks[t], NOSV_SUBMIT_NONE));
	}
	// Wait
	usleep(10000);
	// Check 0
	test_check(&test, atomic_load(&started_tasks) == 0, "Added %d tasks to the submit window only", NTASKS-1);

	// Submit last
	CHECK(nosv_create(&tasks[NTASKS-1], task_type, 0, NOSV_CREATE_NONE));
	CHECK(nosv_submit(tasks[NTASKS-1], NOSV_SUBMIT_NONE));
	// Wait
	int c = 0;
	while ((atomic_load(&started_tasks) != NTASKS) && (c++ < 100))
		usleep(10000);
	// Check NTASKS
	test_check(&test, atomic_load(&started_tasks) == NTASKS, "Flushed the submit window after submitting one more");

	for (int t = 0; t < NTASKS; ++t)
		CHECK(nosv_destroy(tasks[t], NOSV_DESTROY_NONE));
}

// If we set the submit window size to a lower value than the current number of tasks in the window,
// the flush will be executed in the next submit.
void test5()
{
	CHECK(nosv_set_submit_window_size(NTASKS));

	for (int t = 0; t < NTASKS/2; ++t) {
		CHECK(nosv_create(&tasks[t], task_type, 0, NOSV_CREATE_NONE));
		CHECK(nosv_submit(tasks[t], NOSV_SUBMIT_NONE));
	}
	// Wait
	usleep(10000);
	// Check 0
	test_check(&test, atomic_load(&started_tasks) == 0, "Added %d tasks to the submit window only", NTASKS/2);

	CHECK(nosv_set_submit_window_size(1));

	// Submit last
	CHECK(nosv_create(&tasks[NTASKS-1], task_type, 0, NOSV_CREATE_NONE));
	CHECK(nosv_submit(tasks[NTASKS-1], NOSV_SUBMIT_NONE));
	// Wait
	int c = 0;
	while ((atomic_load(&started_tasks) != (NTASKS/2+1)) && (c++ < 100))
		usleep(10000);
	// Check NTASKS
	test_check(&test, atomic_load(&started_tasks) == (NTASKS/2+1), "Flushed the submit window after submitting one task");

	for (int t = 0; t < NTASKS; ++t)
		CHECK(nosv_destroy(tasks[t], NOSV_DESTROY_NONE));
}

int main() {
	test_init(&test, 9);

	CHECK(nosv_init());

	CHECK(nosv_attach(&task_attach, /* affinity */ NULL, NULL, NOSV_ATTACH_NONE));
	// Now we are inside nOS-V

	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));

	test1();
	atomic_store(&started_tasks, 0);

	test2();
	atomic_store(&started_tasks, 0);

	test3();
	atomic_store(&started_tasks, 0);

	test4();
	atomic_store(&started_tasks, 0);

	test5();
	atomic_store(&started_tasks, 0);

	CHECK(nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE));

	CHECK(nosv_detach(NOSV_DETACH_NONE));

	CHECK(nosv_shutdown());

	return 0;
}
