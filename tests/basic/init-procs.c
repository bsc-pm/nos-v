/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"

#include <assert.h>
#include <nosv.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

// Define the MAX_PIDS constant
#include "defaults.h"
#include "generic/signalmutex.h"

typedef struct shmem_data {
	volatile int counter;
	nosv_signal_mutex_t smutex;
} shmem_data_t;

shmem_data_t *data;

int subprocess_body(void)
{
	// Initialize nOS-V
	int err = nosv_init();
	if (err)
		return err;

	nosv_signal_mutex_lock(&data->smutex);

	// Decrease counter of initializers
	int cnt = --data->counter;

	if (cnt == 0) {
		nosv_signal_mutex_broadcast(&data->smutex);
	} else if (cnt > 0) {
		nosv_signal_mutex_wait(&data->smutex);
	} else {
		// The counter may be negative if any process failed
	}
	nosv_signal_mutex_unlock(&data->smutex);

	// Shutdown nOS-V
	err = nosv_shutdown();

	return err;
}

void abort_subprocesses(size_t nprocs, const pid_t pids[nprocs])
{
	for (int p = 0; p < nprocs; ++p) {
		kill(pids[p], SIGKILL);
	}
}

int create_subprocesses(size_t nprocs, pid_t pids[nprocs])
{
	assert(pids != NULL);

	for (int p = 0; p < nprocs; ++p) {
		if ((pids[p] = fork()) < 0) {
			perror("Error in fork");
			abort_subprocesses(p, pids);
			return 1;
		} else if (pids[p] == 0) {
			// Execute the subprocess body
			int err = subprocess_body();

			// Unmap the shared memory data
			munmap(data, sizeof(shmem_data_t));

			exit(err);
		}
	}
	return 0;
}

int wait_subprocesses(size_t nprocs)
{
	int failed = 0;

	for (int p = 0; p < nprocs; ++p) {
		int status;
		pid_t pid = wait(&status);

		if (WIFSIGNALED(status)) {
			// The subprocess received a signal while running. Give some
			// information about the received signal
			fprintf(stderr, "Subprocess %d signaled with %d\n", pid, WTERMSIG(status));
			// Now exit to prevent hanging the tests
			exit(1);
		}

		if (WIFEXITED(status) && WEXITSTATUS(status)) {
			// The subprocess exited or returned but with a non-zero
			// error code. Give some information about the exit status
			fprintf(stderr, "Subprocess %d returned with error %d\n", pid, WEXITSTATUS(status));
			// Now exit to prevent hanging the tests
			exit(WEXITSTATUS(status));
		}
	}

	return failed;
}

int main() {
	const int nprocs = MAX_PIDS;

	pid_t pids[nprocs];

	// Allocate memory for the shared data between processes
	data = mmap(NULL, sizeof(shmem_data_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	assert(data != MAP_FAILED);

	// Initialize the shared counter
	data->counter = nprocs;

	nosv_signal_mutex_init(&data->smutex);

	// Create all nOS-V subprocesses
	int err = create_subprocesses(nprocs, pids);

	// Initialize the test after creating all subprocesses
	test_t test;
	test_init(&test, 1);

	// Disable parallel tests
	test_option(&test, TEST_OPTION_PARALLEL, 0);

	if (err) {
		test_error(&test, "Error while creating multiple subprocesses");
		return 0;
	}

	// Wait all subprocesses and check their exit status
	err = wait_subprocesses(nprocs);

	test_check(&test, !err, "All nOS-V subprocesses finished correctly");

	nosv_signal_mutex_destroy(&data->smutex);

	// Unmap the shared memory data
	munmap(data, sizeof(shmem_data_t));

	return 0;
}
