/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2025 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"
#include "common/utils.h"

#include <nosv.h>
#include <nosv/compat.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <compiler.h>

void exec_attach_instr_after_nosv_init(void)
{
	CHECK(nosv_init());
	nosv_task_t task;

	// abort() here
	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_INSTRUMENT));

	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());
	exit(0);
}

void exec_detach_instr_after_nosv_init(void)
{
	CHECK(nosv_init());
	nosv_task_t task;

	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));

	// abort() here
	CHECK(nosv_detach(NOSV_DETACH_INSTRUMENT));

	CHECK(nosv_shutdown());
	exit(0);
}

#define TH_ATTACH (1 << 0)
#define TH_INSTR_ATT (1 << 1)
#define TH_INSTR_DET (1 << 2)

atomic_uint32_t thread_fini = 0;

static void *thread(void *arg)
{
	nosv_task_t task;
	intptr_t flags = (intptr_t) arg;

	if (flags & TH_ATTACH)
		CHECK(nosv_attach(&task, NULL, "thread", flags & TH_INSTR_ATT ? NOSV_ATTACH_INSTRUMENT : NOSV_ATTACH_NONE));

	if (flags & TH_ATTACH)
		CHECK(nosv_detach(flags & TH_INSTR_DET ? NOSV_DETACH_INSTRUMENT : NOSV_DETACH_NONE));

	atomic_fetch_add(&thread_fini, 1);

	return NULL;
}

void exec_proper_usage(void)
{
	CHECK(nosv_init());
	nosv_task_t task, task_nested;

	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));

	CHECK(nosv_attach(&task_nested, NULL, "main_nested", NOSV_ATTACH_NONE));

	atomic_store(&thread_fini, 0);
	pthread_t pthread;
	CHECK(pthread_create(&pthread, NULL, thread, (void *) (TH_ATTACH | TH_INSTR_ATT | TH_INSTR_DET)));

	CHECK(pthread_join(pthread, NULL));
	assert(atomic_load(&thread_fini) == 1);

	atomic_store(&thread_fini, 0);
	pthread_t pthread_nosv_attach, pthread_nosv_noattach;
	CHECK(nosv_pthread_create(&pthread_nosv_attach, NULL, thread, (void *) (TH_ATTACH)));
	CHECK(nosv_pthread_create(&pthread_nosv_noattach, NULL, thread, NULL));

	while (atomic_load(&thread_fini) != 2)
		nosv_yield(NOSV_YIELD_NONE);

	CHECK(nosv_detach(NOSV_DETACH_NONE));

	CHECK(nosv_detach(NOSV_DETACH_NONE));

	CHECK(nosv_shutdown());
	exit(0);
}

void exec_nosv_pthread_attach_instr(void)
{
	CHECK(nosv_init());
	nosv_task_t task, task_nested;

	atomic_store(&thread_fini, 0);
	pthread_t pthread;
	CHECK(nosv_pthread_create(&pthread, NULL, thread, (void *) (TH_ATTACH | TH_INSTR_ATT)));
	while (atomic_load(&thread_fini) != 1)
		nosv_yield(NOSV_YIELD_NONE);

	CHECK(nosv_shutdown());
	exit(0);
}

void exec_nosv_pthread_detach_instr(void)
{
	CHECK(nosv_init());
	nosv_task_t task, task_nested;

	atomic_store(&thread_fini, 0);
	pthread_t pthread;
	CHECK(nosv_pthread_create(&pthread, NULL, thread, (void *) (TH_ATTACH | TH_INSTR_DET)));
	while (atomic_load(&thread_fini) != 1)
		nosv_yield(NOSV_YIELD_NONE);

	CHECK(nosv_shutdown());
	exit(0);
}


void exec_attach_instr_nested(void)
{
	CHECK(nosv_init());
	nosv_task_t task;


	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());
	exit(0);
}

int main()
{
	test_t test;
	test_init(&test, 5);

	// Disable parallel testing for this
	test_option(&test, TEST_OPTION_PARALLEL, 0);

	pid_t pid;
	int wstatus;
	fflush(stdout);
	pid = fork();

	if (pid == 0)
		exec_attach_instr_after_nosv_init();

	waitpid(pid, &wstatus, 0);
	test_check(&test, WIFSIGNALED(wstatus) && WTERMSIG(wstatus) == SIGABRT, "attach in nosv_init thread aborts");

	fflush(stdout);
	pid = fork();
	if (pid == 0)
		exec_detach_instr_after_nosv_init();

	waitpid(pid, &wstatus, 0);
	test_check(&test, WIFSIGNALED(wstatus) && WTERMSIG(wstatus) == SIGABRT, "detach in nosv_init thread aborts");

	fflush(stdout);
	pid = fork();
	if (pid == 0)
		exec_nosv_pthread_attach_instr();

	waitpid(pid, &wstatus, 0);
	test_check(&test, WIFSIGNALED(wstatus) && WTERMSIG(wstatus) == SIGABRT, "attach+instr on nosv_pthread_create thread aborts");

	fflush(stdout);
	pid = fork();
	if (pid == 0)
		exec_nosv_pthread_detach_instr();

	waitpid(pid, &wstatus, 0);
	test_check(&test, WIFSIGNALED(wstatus) && WTERMSIG(wstatus) == SIGABRT, "detach+instr on nosv_pthread_create thread aborts");

	fflush(stdout);
	pid = fork();
	if (pid == 0)
		exec_proper_usage();

	waitpid(pid, &wstatus, 0);
	test_check(&test, WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0, "proper usage works");
}
