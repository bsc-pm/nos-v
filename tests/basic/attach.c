/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"
#include "common/utils.h"

#include <nosv.h>
#include <sched.h>
#include <stdlib.h>

int main() {
	test_t test;

	test_init(&test, 5);

	putenv("NOSV_CONFIG_OVERRIDE=affinity.compat_support=false");

	CHECK(nosv_init());

	cpu_set_t original, attached, new;

	sched_getaffinity(0, sizeof(cpu_set_t), &original);

	nosv_task_t task;
	CHECK(nosv_attach(&task, NULL, "main" ,NOSV_ATTACH_NONE));
	CHECK(nosv_detach(NOSV_DETACH_NONE));

	sched_getaffinity(0, sizeof(cpu_set_t), &new);
	test_check(&test, CPU_EQUAL(&original, &new), "nosv_detach restores the original thread affinity");

	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	sched_getaffinity(0, sizeof(cpu_set_t), &attached);
	CHECK(nosv_detach(NOSV_DETACH_NO_RESTORE_AFFINITY));

	sched_getaffinity(0, sizeof(cpu_set_t), &new);
	test_check(&test, CPU_EQUAL(&attached, &new), "NOSV_DETACH_NO_RESTORE_AFFINITY skips restoring the original affinity");

	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	nosv_task_t aux = task;
	int ret = nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE);
	test_check(&test, (ret == NOSV_SUCCESS) && (aux == task), "nosv_attach() twice sucess");
	ret = nosv_detach(NOSV_DETACH_NONE);
	test_check(&test, (ret == NOSV_SUCCESS) && (nosv_self() == task), "inner nosv_detach() has not detached");
	ret = nosv_detach(NOSV_DETACH_NONE);
	test_check(&test, (ret == NOSV_SUCCESS) && !nosv_self(), "outer nosv_detach() has detached");

	CHECK(nosv_shutdown());
}
