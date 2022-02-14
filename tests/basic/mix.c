#define _GNU_SOURCE

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

#include "test.h"

// getcpu is only included in GLIBC since 2.29
#if !__GLIBC_PREREQ(2, 29)
static inline int getcpu(unsigned int *cpu, unsigned int *node)
{
	return syscall(SYS_getcpu, cpu, node);
}
#endif

atomic_int tasks_run;
nosv_task_t task_attach;
nosv_task_t task_immediate;
test_t t;

_Thread_local unsigned int test = 0;

void run_deadline(nosv_task_t task)
{
	nosv_waitfor(1000000ULL, NULL);
}

void complete_deadline(nosv_task_t task)
{
	test_ok(&t, "Completed deadline task");

	nosv_destroy(task, NOSV_DESTROY_NONE);

	int r = atomic_fetch_sub_explicit(&tasks_run, 1, memory_order_relaxed) - 1;
	if (!r)
		nosv_submit(task_attach, NOSV_SUBMIT_UNLOCKED);
}

void run_immediate(nosv_task_t task)
{
	// Placeholder, as magic is done from the completed
}

void completed_imm(nosv_task_t task)
{
	test = 1;
	nosv_submit(task_immediate, NOSV_SUBMIT_IMMEDIATE);
	usleep(5000);
	test = 2;

	// printf("Event %x\n", task);
	nosv_destroy(task, NOSV_DESTROY_NONE);

	int r = atomic_fetch_sub_explicit(&tasks_run, 1, memory_order_relaxed) - 1;
	assert(r);
}

void run_immediate_2(nosv_task_t task)
{
	test_check(&t, test == 2, "Immediate successor order is correct");
}

void run(nosv_task_t task)
{
	unsigned int cpu;
	getcpu(&cpu, NULL);

	int *metadata = nosv_get_task_metadata(task);
	assert(*metadata == -1 || *metadata == cpu);
}

void completed(nosv_task_t task)
{
	// printf("Event %x\n", task);
	nosv_destroy(task, NOSV_DESTROY_NONE);

	int r = atomic_fetch_sub_explicit(&tasks_run, 1, memory_order_relaxed) - 1;
	if (!r)
		nosv_submit(task_attach, NOSV_SUBMIT_UNLOCKED);
}

void run_cost(nosv_task_t task)
{
	nosv_waitfor(100000ULL, NULL);
}

void completed_cost(nosv_task_t task)
{
	test_ok(&t, "Completed cost task");

	nosv_destroy(task, NOSV_DESTROY_NONE);

	int r = atomic_fetch_sub_explicit(&tasks_run, 1, memory_order_relaxed) - 1;
	if (!r)
		nosv_submit(task_attach, NOSV_SUBMIT_UNLOCKED);
}

uint64_t cost_function(nosv_task_t task)
{
	return 42;
}

int main()
{
	test_init(&t, 5);
	atomic_init(&tasks_run, 105);

	nosv_init();

	nosv_task_type_t adopted_type;
	int res = nosv_type_init(&adopted_type, NULL, NULL, NULL, NULL, NULL, NOSV_TYPE_INIT_EXTERNAL, NULL);
	assert(!res);

	res = nosv_attach(&task_attach, adopted_type, 0, /* affinity */ NULL, NOSV_ATTACH_NONE);
	assert(!res);
	// Now we are inside nOS-V

	nosv_task_type_t deadline_type;
	res = nosv_type_init(&deadline_type, &run_deadline, NULL, &complete_deadline, NULL, NULL, NOSV_TYPE_INIT_NONE, NULL);
	assert(!res);
	nosv_task_t deadline_task;
	res = nosv_create(&deadline_task, deadline_type, 0, NOSV_CREATE_NONE);
	assert(!res);
	res = nosv_submit(deadline_task, NOSV_SUBMIT_NONE);
	assert(!res);

	nosv_task_type_t type;
	nosv_task_t task;

	res = nosv_type_init(&type, &run, NULL /* end */, &completed, NULL, NULL, NOSV_TYPE_INIT_NONE, NULL);
	assert(!res);

	// Number of available CPUs
	int cpus = test_get_cpus();
	// Array containing all the system CPU ids
	int *cpu_indexes = test_get_cpu_array();

	for (int i = 0; i < 100; ++i)
	{
		res = nosv_create(&task, type, sizeof(int), NOSV_CREATE_NONE);
		assert(!res);

		// Get system CPU id
		int cpu = cpu_indexes[i % cpus];
		int *metadata = nosv_get_task_metadata(task);

		*metadata = cpu;
		nosv_affinity_t aff = nosv_affinity_get(cpu, NOSV_AFFINITY_LEVEL_CPU, NOSV_AFFINITY_TYPE_STRICT);
		nosv_set_task_affinity(task, &aff);

		nosv_submit(task, NOSV_SUBMIT_NONE);
	}

	free(cpu_indexes);

	res = nosv_create(&task, type, sizeof(int), NOSV_CREATE_NONE);
	assert(!res);
	int *metadata = nosv_get_task_metadata(task);
	*metadata = -1;
	nosv_submit(task, NOSV_SUBMIT_BLOCKING);
	test_ok(&t, "Task unlocked from submit blocking");

	nosv_submit(task, NOSV_SUBMIT_NONE);

	nosv_task_type_t imm1, imm2;
	res = nosv_type_init(&imm1, &run_immediate, NULL, &completed_imm, NULL, NULL, NOSV_TYPE_INIT_NONE, NULL);
	assert(!res);
	res = nosv_type_init(&imm2, &run_immediate_2, NULL, &completed, NULL, NULL, NOSV_TYPE_INIT_NONE, NULL);
	assert(!res);

	res = nosv_create(&task, imm1, 0, NOSV_CREATE_NONE);
	assert(!res);
	res = nosv_create(&task_immediate, imm2, 0, NOSV_CREATE_NONE);
	assert(!res);

	nosv_submit(task, NOSV_SUBMIT_NONE);

	// Simple cost test
	nosv_task_type_t cost_type;
	res = nosv_type_init(&cost_type, &run_cost, NULL, &completed_cost, NULL, NULL, NOSV_TYPE_INIT_NONE, cost_function);
	assert(!res);

	res = nosv_create(&task, cost_type, 0, NOSV_CREATE_NONE);
	assert(!res);

	nosv_submit(task, NOSV_SUBMIT_NONE);

	res = nosv_pause(NOSV_PAUSE_NONE);
	assert(!res);
	test_ok(&t, "Task unpaused");

	res = nosv_detach(NOSV_DETACH_NONE);
	assert(!res);

	nosv_type_destroy(adopted_type, NOSV_TYPE_DESTROY_NONE);
	nosv_type_destroy(type, NOSV_TYPE_DESTROY_NONE);
	nosv_type_destroy(deadline_type, NOSV_TYPE_DESTROY_NONE);
	nosv_type_destroy(cost_type, NOSV_TYPE_DESTROY_NONE);
	nosv_type_destroy(imm1, NOSV_TYPE_DESTROY_NONE);
	nosv_type_destroy(imm2, NOSV_TYPE_DESTROY_NONE);

	nosv_shutdown();

	test_end(&t);
	return 0;
}
