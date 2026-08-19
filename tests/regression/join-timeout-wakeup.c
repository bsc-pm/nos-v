/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2026 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <dlfcn.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

#include <nosv.h>
#include <nosv/affinity.h>

#include "common/utils.h"
#include "test.h"

// 100ms
#define FIRST_JOIN_TIMEOUT_NS (100ULL * 1000ULL * 1000ULL)
// 1s
#define DELAYED_TASK_NS (1000ULL * 1000ULL * 1000ULL)
// 5s
#define SECOND_JOIN_TIMEOUT_NS (5ULL * 1000ULL * 1000ULL * 1000ULL)
// 200 ms
#define WAKEUP_DELAY_US (200ULL * 1000ULL)

typedef int (*submit_fn_t)(nosv_task_t, nosv_flags_t);

static atomic_int delay_wakeup = 1;
static submit_fn_t real_submit;
static nosv_task_t delayed_task;

// Hack to introduce a delay in nosv_submit
int nosv_submit(nosv_task_t task, nosv_flags_t flags)
{
	if ((flags & NOSV_SUBMIT_DEADLINE_WAKE) && atomic_exchange(&delay_wakeup, 0))
		usleep(WAKEUP_DELAY_US);

	return real_submit(task, flags);
}

static void run(nosv_task_t task)
{
	if (task == delayed_task)
		CHECK(nosv_waitfor(DELAYED_TASK_NS, NULL));
}


// NOTE: This test will only fail in the "bad" case when using 2 or more CPUs
// It relies on task 0's wakeup being delayed for at least the timeout, allowing the main task
// to be woken up in the meantime, and moving to another join.
int main(void)
{
	real_submit = (submit_fn_t) dlsym(RTLD_NEXT, "nosv_submit");
	assert(real_submit);

	test_t test;
	test_init(&test, 2);
	test_option(&test, TEST_OPTION_PARALLEL, 0);

	CHECK(nosv_init());

	nosv_task_t attached_task;
	CHECK(nosv_attach(&attached_task, NULL, "main", NOSV_ATTACH_NONE));

	nosv_task_type_t task_type;
	CHECK(nosv_type_init(&task_type, run, NULL, NULL, "task", NULL, NULL, NOSV_TYPE_INIT_NONE));

	nosv_task_t tasks[2];
	CHECK(nosv_create(&tasks[0], task_type, 0, NOSV_CREATE_JOINABLE));
	CHECK(nosv_create(&tasks[1], task_type, 0, NOSV_CREATE_JOINABLE));
	delayed_task = tasks[1];

	// Keep the immediate task on the CPU currently executing the attached task.
	// This guarantees that it cannot complete until join_all pauses that task.
	nosv_affinity_t affinity = nosv_affinity_get(sched_getcpu(), NOSV_AFFINITY_LEVEL_CPU, NOSV_AFFINITY_TYPE_STRICT);
	nosv_set_task_affinity(tasks[0], &affinity);

	CHECK(nosv_submit(tasks[0], NOSV_SUBMIT_NONE));
	CHECK(nosv_submit(tasks[1], NOSV_SUBMIT_NONE));

	int first_result = nosv_join_all(tasks, NULL, 2, FIRST_JOIN_TIMEOUT_NS);
	test_check(&test, first_result == NOSV_ERR_TIMEOUT, "First join returns a timeout");

	int second_result = nosv_join_all(tasks, NULL, 2, SECOND_JOIN_TIMEOUT_NS);
	test_check(&test, second_result == NOSV_SUCCESS, "Second join successfully waits");

	if (second_result != NOSV_SUCCESS)
		CHECK(nosv_join_all(tasks, NULL, 2, (uint64_t) -1));

	CHECK(nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());

	test_end(&test);
	return 0;
}
