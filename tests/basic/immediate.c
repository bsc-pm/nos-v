/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"

#include <nosv.h>
#include <nosv/affinity.h>
#include <stdatomic.h>
#include <unistd.h>

test_t test;
nosv_task_t ext_task;

atomic_int completed_counter = 0;
atomic_int order_counter = 0;

void task_run_IS(nosv_task_t task)
{
	test_check(&test, atomic_fetch_add(&order_counter, 1) == 0, "IS executed before higher priority task");
}

void task_run(nosv_task_t task)
{
	test_check(&test, atomic_fetch_add(&order_counter, 1) == 1, "Task executed after immediate successor");
}

void task_comp(nosv_task_t task)
{
	atomic_fetch_add(&completed_counter, 1);
}

void task_comp_and_submit(nosv_task_t task)
{
	atomic_fetch_add(&completed_counter, 1);

	CHECK(nosv_submit(ext_task, NOSV_SUBMIT_NONE));
}

void reset_counters()
{
	atomic_store(&order_counter,0);
	atomic_store(&completed_counter,0);
}

int main()
{
	test_init(&test, 5 + 5 + 5);
	CHECK(nosv_init());

	nosv_affinity_t aff = nosv_affinity_get(get_first_cpu(), NOSV_AFFINITY_LEVEL_CPU, NOSV_AFFINITY_TYPE_STRICT);

	CHECK(nosv_attach(&ext_task, &aff, NULL, NOSV_ATTACH_NONE));

	nosv_task_type_t type, typeIS;
	CHECK(nosv_type_init(&type, task_run, NULL, task_comp, "Test", NULL, NULL, NOSV_TYPE_INIT_NONE));
	CHECK(nosv_type_init(&typeIS, task_run_IS, NULL, task_comp, "Test", NULL, NULL, NOSV_TYPE_INIT_NONE));

	nosv_task_t taskNoIS, taskIS;
	CHECK(nosv_create(&taskNoIS, type, 0, NOSV_CREATE_NONE));
	nosv_set_task_affinity(taskNoIS, &aff);
	CHECK(nosv_create(&taskIS, typeIS, 0, NOSV_CREATE_NONE));
	nosv_set_task_affinity(taskIS, &aff);

	// Submit + Submit IS + nosv_yield
	CHECK(nosv_submit(taskNoIS, NOSV_SUBMIT_NONE));
	CHECK(nosv_submit(taskIS, NOSV_SUBMIT_IMMEDIATE));

	usleep(20000);
	test_check(&test, atomic_load(&order_counter) == 0, "No tasks executed serializing execution");

	CHECK(nosv_yield(NOSV_YIELD_NONE));

	// As we share the same affinity with the other tasks, after the yield both tasks should be executed
	test_check(&test, atomic_load(&order_counter) == 2, "All tasks executed before returning to main");

	test_check(&test, atomic_load(&completed_counter) == 2, "Tasks completed");
	reset_counters();
	
	// Submit + Submit IS + nosv_schedpoint
	CHECK(nosv_submit(taskNoIS, NOSV_SUBMIT_NONE));
	CHECK(nosv_submit(taskIS, NOSV_SUBMIT_IMMEDIATE));
	
	usleep(20000); 
	test_check(&test, atomic_load(&order_counter) == 0, "No tasks executed serializing execution");

	// Make sure to exhaust quantum (defalult value = 20000000 nanosec)
	usleep(20000); 
	CHECK(nosv_schedpoint(NOSV_SCHEDPOINT_NONE));

	// As we share the same affinity with the other tasks, after the yield both tasks should be executed
	test_check(&test, atomic_load(&order_counter) == 2, "All tasks executed before returning to main");

	test_check(&test, atomic_load(&completed_counter) == 2, "Tasks completed");
	reset_counters();
	
	// Submit + Submit IS + nosv_pause
	nosv_task_type_t type2;
	CHECK(nosv_type_init(&type2, task_run, NULL, task_comp_and_submit, "Test", NULL, NULL, NOSV_TYPE_INIT_NONE));

	nosv_task_t taskNoIS2;
	CHECK(nosv_create(&taskNoIS2, type2, 0, NOSV_CREATE_NONE));
	nosv_set_task_affinity(taskNoIS2, &aff);

	CHECK(nosv_submit(taskNoIS2, NOSV_SUBMIT_NONE));
	CHECK(nosv_submit(taskIS, NOSV_SUBMIT_IMMEDIATE));

	usleep(20000);
	test_check(&test, atomic_load(&order_counter) == 0, "No tasks executed serializing execution");

	CHECK(nosv_pause(NOSV_PAUSE_NONE));

	// As we share the same affinity with the other tasks, after the yield both tasks should be executed
	test_check(&test, atomic_load(&order_counter) == 2, "All tasks executed before returning to main");

	test_check(&test, atomic_load(&completed_counter) == 2, "Tasks completed");
	reset_counters();

	CHECK(nosv_detach(NOSV_DETACH_NONE));

	CHECK(nosv_destroy(taskIS, NOSV_DESTROY_NONE));
	CHECK(nosv_destroy(taskNoIS, NOSV_DESTROY_NONE));
	CHECK(nosv_destroy(taskNoIS2, NOSV_DESTROY_NONE));

	CHECK(nosv_type_destroy(type, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_type_destroy(typeIS, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_type_destroy(type2, NOSV_TYPE_DESTROY_NONE));

	CHECK(nosv_shutdown());
}
