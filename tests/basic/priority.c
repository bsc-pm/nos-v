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

#define NTASKS 10
#define NTASKS_PER_PRIO 4

atomic_int nr_completed_tasks;
atomic_int expected_prio = 0;

nosv_task_t tasks[NTASKS * NTASKS_PER_PRIO];

test_t test;

void task_run(nosv_task_t task)
{
	printf("%p\n", task);
	int current_priority = nosv_get_task_priority(task);
	int last_priority = atomic_load(&expected_prio);
	test_check(&test, last_priority == current_priority || last_priority == current_priority - 1,
		"Task ran in strict priority order (expect %d or %d got %d)",
		current_priority, current_priority - 1, last_priority);
	atomic_store(&expected_prio, current_priority - 1);
}

void task_comp(nosv_task_t task)
{
	atomic_fetch_add(&nr_completed_tasks, 1);
}

int main() {
	test_init(&test, (NTASKS * NTASKS_PER_PRIO) + 1);

	CHECK(nosv_init());

	// Attach ourselves in CPU 0
	nosv_affinity_t single_cpu = nosv_affinity_get(get_first_cpu(), NOSV_AFFINITY_LEVEL_CPU, NOSV_AFFINITY_TYPE_STRICT);
	nosv_task_t task_attach;
	CHECK(nosv_attach(&task_attach, &single_cpu, NULL, NOSV_ATTACH_NONE));

	// Inside CPU 0 in nOS-V
	nosv_task_type_t task_type;
	CHECK(nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));

	// The submitted tasks will execute after we yield, since they also are on CPU 0
	for (int t = 0; t < NTASKS; ++t) {
		for (int p = 0; p < NTASKS_PER_PRIO; ++p) {
			int idx = t * NTASKS_PER_PRIO + p;
			CHECK(nosv_create(&tasks[idx], task_type, 0, NOSV_CREATE_NONE));
			nosv_set_task_priority(tasks[idx], t+1);
			nosv_set_task_affinity(tasks[idx], &single_cpu);
			CHECK(nosv_submit(tasks[idx], NOSV_SUBMIT_NONE));
		}
	}

	atomic_store(&expected_prio, NTASKS);

	// A bit hacky. We have to wait until the thread in the scheduler inserts ALL priority tasks, 
	// otherwise the order is going to be wrong
	usleep(10000);

	CHECK(nosv_yield(NOSV_YIELD_NONE));

	test_check(&test, atomic_load(&nr_completed_tasks) == (NTASKS * NTASKS_PER_PRIO), "Could run all tasks with priorities");

	for (int t = 0; t < NTASKS * NTASKS_PER_PRIO; ++t)
		CHECK(nosv_destroy(tasks[t], NOSV_DESTROY_NONE));

	CHECK(nosv_detach(NOSV_DETACH_NONE));

	CHECK(nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE));

	CHECK(nosv_shutdown());

	return 0;
}
