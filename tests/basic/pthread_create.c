/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2025 Barcelona Supercomputing Center (BSC)
*/

#include <limits.h>
#include <nosv.h>
#include <nosv/compat.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test.h"
#include "common/utils.h"

test_t test;
nosv_task_type_t task;

atomic_int result = 0;

void *thread_routine(void *arg)
{
	assert(arg);
	atomic_store(&result, *(int *) arg);

	return NULL;
}

int main()
{
	nosv_task_t task;

	test_init(&test, 1);

	CHECK(nosv_init());
	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));

	pthread_t thread;
	int arg = 42;
	CHECK(nosv_pthread_create(&thread, NULL, &thread_routine, &arg));

	test_check_waitfor(&test, atomic_load(&result) == arg, 100, "ok");
	// If we reached this point, the wrapper should detach and we can join the pthread
	CHECK(pthread_join(thread, NULL));

	// Shutdown nosv
	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());
}
