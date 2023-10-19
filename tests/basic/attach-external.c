/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"
#include "common/utils.h"

#include <nosv.h>
#include <pthread.h>
#include <sched.h>

static nosv_task_type_t type;
static test_t test;


static void *start_routine(void *arg)
{
	nosv_task_t task;

	CHECK(nosv_attach(&task, type, 0, NULL, NOSV_ATTACH_NONE));
	test_ok(&test, "External thread can attach");

	CHECK(nosv_detach(NOSV_DETACH_NONE));
	test_ok(&test, "External thread can detach");

	return NULL;
}

int main()
{
	test_init(&test, 4);

	CHECK(nosv_init());

	CHECK(nosv_type_init(&type, NULL, NULL, NULL, "main", NULL, NULL, NOSV_TYPE_INIT_EXTERNAL));

	pthread_t external_thread;
	pthread_create(&external_thread, NULL, &start_routine, NULL);

	nosv_task_t task;
	CHECK(nosv_attach(&task, type, 0, NULL, NOSV_ATTACH_NONE));
	test_ok(&test, "Main thread can attach");
	CHECK(nosv_detach(NOSV_DETACH_NONE));
	test_ok(&test, "Main thread can detach");

	pthread_join(external_thread, NULL);

	CHECK(nosv_type_destroy(type, NOSV_DESTROY_NONE));
	CHECK(nosv_shutdown());
}
