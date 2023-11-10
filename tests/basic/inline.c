/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"
#include "common/utils.h"

#include <nosv.h>
#include <sys/syscall.h>
#include <unistd.h>

#if !__GLIBC_PREREQ(2, 30)
static inline pid_t gettid(void)
{
	return syscall(SYS_gettid);
}
#endif

int tid;
int exec;
test_t test;

void worker_run(nosv_task_t task)
{
	test_check(&test, tid == gettid(), "Task executed in the same thread");
	usleep(5000);
	exec = 1;
}

void worker_comp(nosv_task_t task)
{
	CHECK(nosv_destroy(task, NOSV_DESTROY_NONE));
}

int main() {
	test_init(&test, 2);

	CHECK(nosv_init());

	nosv_task_t ext_task;
	CHECK(nosv_attach(&ext_task, NULL, NULL, NOSV_ATTACH_NONE));

	nosv_task_type_t worker;
	CHECK(nosv_type_init(&worker, worker_run, NULL, worker_comp, "worker", NULL, NULL, NOSV_TYPE_INIT_NONE));
	nosv_task_t worker_task;
	CHECK(nosv_create(&worker_task, worker, 0, NOSV_CREATE_NONE));

	tid = gettid();
	CHECK(nosv_submit(worker_task, NOSV_SUBMIT_INLINE));
	CHECK(nosv_type_destroy(worker, NOSV_TYPE_DESTROY_NONE));

	test_check(&test, exec == 1, "Execution order respected");
	CHECK(nosv_detach(NOSV_DETACH_NONE));

	CHECK(nosv_shutdown());
}
