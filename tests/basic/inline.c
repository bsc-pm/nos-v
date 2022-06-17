/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"

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
	nosv_destroy(task, NOSV_DESTROY_NONE);
}

int main() {
	test_init(&test, 2);

	nosv_init();

	nosv_task_type_t ext;
	int res = nosv_type_init(&ext, NULL, NULL, NULL, NULL, NULL, NULL, NOSV_TYPE_INIT_EXTERNAL);
	assert(!res);

	nosv_task_t ext_task;
	res = nosv_attach(&ext_task, ext, 0, NULL, NOSV_ATTACH_NONE);
	assert(!res);

	nosv_task_type_t worker;
	res = nosv_type_init(&worker, worker_run, NULL, worker_comp, "worker", NULL, NULL, NOSV_TYPE_INIT_NONE);
	assert(!res);
	nosv_task_t worker_task;
	nosv_create(&worker_task, worker, 0, NOSV_CREATE_NONE);

	tid = gettid();
	nosv_submit(worker_task, NOSV_SUBMIT_INLINE);
	nosv_type_destroy(worker, NOSV_TYPE_DESTROY_NONE);

	test_check(&test, exec == 1, "Execution order respected");
	nosv_detach(NOSV_DETACH_NONE);
	nosv_type_destroy(ext, NOSV_TYPE_DESTROY_NONE);

	nosv_shutdown();
}
