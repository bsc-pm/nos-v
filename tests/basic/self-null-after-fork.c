/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2026 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"
#include "common/utils.h"

#include <nosv.h>
#include <sys/wait.h>
#include <unistd.h>

int main()
{
	test_t test;
	test_init(&test, 1);
	test_option(&test, TEST_OPTION_PARALLEL, 0);

	CHECK(nosv_init());

	nosv_task_t task;
	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));

	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0)
		_exit(nosv_self() == NULL ? 0 : 1);

	int wstatus;
	if (pid < 0) {
		perror("fork");
		CHECK(nosv_detach(NOSV_DETACH_NONE));
		CHECK(nosv_shutdown());
		return 1;
	}

	int ret = waitpid(pid, &wstatus, 0);
	if (ret != pid)
		test_error(&test, "unexpected return from waitpid");

	test_check(&test, WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0, "nosv_self() is NULL after fork");

	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());
}
