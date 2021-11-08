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

atomic_int *counter;

int subprocess_body(void)
{
	// Initialize nOS-V
	int err = nosv_init();
	if (err)
		return err;

	// Decrease counter of initializers
	atomic_fetch_sub(counter, 1);

	// Wait until all subprocesses initialize
	while (atomic_load(counter) > 0) {
		usleep(1000);
	}

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

			// Unmap the counter memory
			munmap(counter, sizeof(atomic_int));

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
		}

		if (WIFEXITED(status) && WEXITSTATUS(status)) {
			// The subprocess exited or returned but with a non-zero
			// error code. Give some information about the exit status
			fprintf(stderr, "Subprocess %d returned with error %d\n", pid, WEXITSTATUS(status));
		}

		if (!failed && (!WIFEXITED(status) || WEXITSTATUS(status))) {
			// One of the processes failed for some reason. Let the rest of processes
			// exit normally and wait for them to finish
			atomic_store(counter, 0);
			failed = 1;
		}
	}

	return failed;
}

int main() {
	const int nprocs = MAX_PIDS;

	pid_t pids[nprocs];

	// Allocate memory for a shared atomic counter between processes
	counter = mmap(NULL, sizeof(atomic_int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	assert(counter != MAP_FAILED);

	// Initialize the atomic counter
	*counter = nprocs;

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

	// Unmap the counter memory
	munmap(counter, sizeof(atomic_int));

	return 0;
}
