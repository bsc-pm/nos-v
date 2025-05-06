/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"
#include "common/utils.h"

#include <nosv.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "generic/arch.h"

#ifdef ARCH_HAS_TURBO

// This test expects turbo to be enabled in the .toml
void exec_child_crash(void) {
	CHECK(nosv_init());
	nosv_task_t task;

	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	// Turbo enabled in main thread

	arch_configure_turbo(0);

	// exit(1) here
	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());
	exit(0);
}

void exec_child_crash1(void) {
	CHECK(nosv_init());
	nosv_task_t task;

	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	// Turbo enabled in main thread

	arch_configure_turbo(0);

	// exit(1) here
	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));


	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());
	exit(0);
}

void exec_child_crash2(void) {
	CHECK(nosv_init());
	nosv_task_t task;

	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	// Turbo enabled in main thread

	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	CHECK(nosv_detach(NOSV_DETACH_NONE));

	arch_configure_turbo(0);

	// exit(1) here
	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());
	exit(0);
}

void exec_child_nocrash(void) {
	CHECK(nosv_init());
	nosv_task_t task;

	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	// Turbo enabled in main thread

	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());
	exit(0);
}

int main() {
	test_t test;

	test_init(&test, 4);
	// Disable parallel testing for this
	test_option(&test, TEST_OPTION_PARALLEL, 0);

	pid_t pid;
	int wstatus;
	fflush(stdout);
	pid = fork();

	if (pid == 0)
		exec_child_crash();

	waitpid(pid, &wstatus, 0);
	test_check(&test, WIFSIGNALED(wstatus) && WTERMSIG(wstatus) == SIGABRT, "Got an turbo change between nosv_attach and nosv_detach");

	fflush(stdout);
	pid = fork();
	if (pid == 0)
		exec_child_crash1();

	waitpid(pid, &wstatus, 0);
	test_check(&test, WIFSIGNALED(wstatus) && WTERMSIG(wstatus) == SIGABRT, "Got an turbo change between nosv_attach and nosv_attach");

	fflush(stdout);
	pid = fork();
	if (pid == 0)
		exec_child_crash2();

	waitpid(pid, &wstatus, 0);
	test_check(&test, WIFSIGNALED(wstatus) && WTERMSIG(wstatus) == SIGABRT, "Got an turbo change between nosv_detach and nosv_detach");

	fflush(stdout);
	pid = fork();
	if (pid == 0)
		exec_child_nocrash();

	waitpid(pid, &wstatus, 0);
	test_check(&test, WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0, "Cleaned up and exited normally");
}

#else // ARCH_HAS_TURBO

int main() {
	return 0;
}

#endif  // ARCH_HAS_TURBO
