#include "test.h"

#include <nosv.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static inline void exec_child_crash(void)
{
	// Initialize nOS-V and "crash"
	nosv_init();
	exit(0);
}

static inline void exec_child_nocrash(void)
{
	// Initialize and exit correctly
	nosv_init();
	nosv_shutdown();
	exit(0);
}

int main() {
	test_t test;

	test_init(&test, 2);
	// Disable parallel testing for this
	test_option(&test, TEST_OPTION_PARALLEL, 0);

	pid_t pid;
	int wstatus;
	fflush(stdout);
	pid = fork();

	if (pid == 0)
		exec_child_crash();

	waitpid(pid, &wstatus, 0);
	test_check(&test, WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0, "Left garbage in shared memory");

	fflush(stdout);
	pid = fork();
	if (pid == 0)
		exec_child_nocrash();

	waitpid(pid, &wstatus, 0);
	test_check(&test, WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0, "Cleaned up and exited normally");
}
