/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <semaphore.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <nosv.h>
#include <nosv/affinity.h>
#include <nosv/hwinfo.h>

#include "test.h"
#include "common/utils.h"

// getcpu is only included in GLIBC since 2.29
#if !__GLIBC_PREREQ(2, 29)

// Fix for very old Linux versions
#ifndef SYS_getcpu
#define SYS_getcpu 0
#define SKIP_CPU_TEST 1
#endif

static inline int getcpu(unsigned int *cpu, unsigned int *node)
{
	return syscall(SYS_getcpu, cpu, node);
}
#endif

atomic_int tasks_run;
nosv_task_t task_attach;
nosv_task_t task_immediate;
test_t t;

volatile unsigned int test = 0;

void run_deadline(nosv_task_t task)
{
	nosv_waitfor(1000000ULL, NULL);
}

void complete_deadline(nosv_task_t task)
{
	test_ok(&t, "Completed deadline task");

	CHECK(nosv_destroy(task, NOSV_DESTROY_NONE));

	int r = atomic_fetch_sub_explicit(&tasks_run, 1, memory_order_relaxed) - 1;
	if (!r)
		CHECK(nosv_submit(task_attach, NOSV_SUBMIT_UNLOCKED));
}

void run_immediate(nosv_task_t task)
{
	// Placeholder, as magic is done from the completed
}

void completed_imm(nosv_task_t task)
{
	test = 1;
	CHECK(nosv_submit(task_immediate, NOSV_SUBMIT_IMMEDIATE));
	usleep(5000);
	test = 2;

	// printf("Event %x\n", task);
	CHECK(nosv_destroy(task, NOSV_DESTROY_NONE));

	int r = atomic_fetch_sub_explicit(&tasks_run, 1, memory_order_relaxed) - 1;
	assert(r);
}

void run_immediate_2(nosv_task_t task)
{
	test_check(&t, test == 2, "Immediate successor order is correct");
}

void run(nosv_task_t task)
{
#ifndef SKIP_CPU_TEST
	unsigned int cpu;
	getcpu(&cpu, NULL);

	int *metadata = nosv_get_task_metadata(task);
	assert(*metadata == -1 || *metadata == cpu);
#endif
}

void completed(nosv_task_t task)
{
	// printf("Event %x\n", task);
	CHECK(nosv_destroy(task, NOSV_DESTROY_NONE));

	int r = atomic_fetch_sub_explicit(&tasks_run, 1, memory_order_relaxed) - 1;
	if (!r)
		CHECK(nosv_submit(task_attach, NOSV_SUBMIT_UNLOCKED));
}

void run_cost(nosv_task_t task)
{
	nosv_waitfor(100000ULL, NULL);
}

void completed_cost(nosv_task_t task)
{
	test_ok(&t, "Completed cost task");

	CHECK(nosv_destroy(task, NOSV_DESTROY_NONE));

	int r = atomic_fetch_sub_explicit(&tasks_run, 1, memory_order_relaxed) - 1;
	if (!r)
		CHECK(nosv_submit(task_attach, NOSV_SUBMIT_UNLOCKED));
}

uint64_t cost_function(nosv_task_t task)
{
	return 42;
}

int main()
{
	test_init(&t, 5);
	atomic_init(&tasks_run, 105);

	CHECK(nosv_init());

	CHECK(nosv_attach(&task_attach, /* affinity */ NULL, NULL, NOSV_ATTACH_NONE));
	// Now we are inside nOS-V

	nosv_task_type_t deadline_type;
	CHECK(nosv_type_init(&deadline_type, &run_deadline, NULL, &complete_deadline, NULL, NULL, NULL, NOSV_TYPE_INIT_NONE));
	nosv_task_t deadline_task;
	CHECK(nosv_create(&deadline_task, deadline_type, 0, NOSV_CREATE_NONE));
	CHECK(nosv_submit(deadline_task, NOSV_SUBMIT_NONE));

	nosv_task_type_t type;
	nosv_task_t task;

	CHECK(nosv_type_init(&type, &run, NULL /* end */, &completed, NULL, NULL, NULL, NOSV_TYPE_INIT_NONE));

	// Number of available CPUs
	int cpus = nosv_get_num_cpus();
	// Array containing all the system CPU ids
	int *cpu_indexes = nosv_get_available_cpus();

	for (int i = 0; i < 100; ++i) {
		CHECK(nosv_create(&task, type, sizeof(int), NOSV_CREATE_NONE));

		// Get system CPU id
		int cpu = cpu_indexes[i % cpus];
		int *metadata = nosv_get_task_metadata(task);

		*metadata = cpu;
		nosv_affinity_t aff = nosv_affinity_get(cpu, NOSV_AFFINITY_LEVEL_CPU, NOSV_AFFINITY_TYPE_STRICT);
		nosv_set_task_affinity(task, &aff);

		CHECK(nosv_submit(task, NOSV_SUBMIT_NONE));
	}

	CHECK(nosv_create(&task, type, sizeof(int), NOSV_CREATE_NONE));
	int *metadata = nosv_get_task_metadata(task);
	*metadata = -1;
	CHECK(nosv_submit(task, NOSV_SUBMIT_BLOCKING));
	test_ok(&t, "Task unlocked from submit blocking");

	nosv_task_type_t imm1, imm2;
	CHECK(nosv_type_init(&imm1, &run_immediate, NULL, &completed_imm, NULL, NULL, NULL, NOSV_TYPE_INIT_NONE));
	CHECK(nosv_type_init(&imm2, &run_immediate_2, NULL, &completed, NULL, NULL, NULL, NOSV_TYPE_INIT_NONE));

	CHECK(nosv_create(&task, imm1, 0, NOSV_CREATE_NONE));
	CHECK(nosv_create(&task_immediate, imm2, 0, NOSV_CREATE_NONE));

	CHECK(nosv_submit(task, NOSV_SUBMIT_NONE));

	// Simple cost test
	nosv_task_type_t cost_type;
	CHECK(nosv_type_init(&cost_type, &run_cost, NULL, &completed_cost, NULL, NULL, cost_function, NOSV_TYPE_INIT_NONE));

	CHECK(nosv_create(&task, cost_type, 0, NOSV_CREATE_NONE));

	CHECK(nosv_submit(task, NOSV_SUBMIT_NONE));

	CHECK(nosv_pause(NOSV_PAUSE_NONE));
	test_ok(&t, "Task unpaused");

	CHECK(nosv_detach(NOSV_DETACH_NONE));

	CHECK(nosv_type_destroy(type, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_type_destroy(deadline_type, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_type_destroy(cost_type, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_type_destroy(imm1, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_type_destroy(imm2, NOSV_TYPE_DESTROY_NONE));

	CHECK(nosv_shutdown());

	test_end(&t);
	return 0;
}
