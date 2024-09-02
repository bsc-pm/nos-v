/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

#include "tap.h"

#define PARALLEL_TESTS 8
#define MAX_BUFF_SIZE 2048

#define __unused __attribute__((unused))

#define OPTION_PARALLEL 0

enum test_status {
	PASS = 0,
	XFAIL = 1,
	SKIP = 2,
	FAIL = 3,
	ERROR = 4
};

struct test_outcome {
	enum test_status status;
	const char *description;
	const char *reason;
};

struct test_harness {
	int ntests;
	int retval;
	int curr;
	struct test_outcome *outcomes;
};

struct harness_args {
	const char *program;
	struct test_harness *result;
	struct test_options *options;
};

struct test_options {
	int parallel;
};

struct test_proc_descriptor {
	pid_t pid;
	int readfd;
	int finished;
	char *output;
	size_t size;
	size_t off;
};

static inline void usage(const char *program)
{
	printf("Usage: driver %s <test-to-execute>\n", program);
	exit(1);
}

#define _errstr(fmt, ...) fprintf(stderr, fmt "%s\n", ##__VA_ARGS__)
#define report_error(...) _report_error(__VA_ARGS__, "")
#define _report_error(fmt, ...)                                                                          \
	do {                                                                                                 \
		if (errno)                                                                                       \
			_errstr("TEST DRIVER ERROR in %s(): " fmt ": %s", __func__, ##__VA_ARGS__, strerror(errno)); \
		else                                                                                             \
			_errstr("TEST DRIVER ERROR in %s(): " fmt, __func__, ##__VA_ARGS__);                         \
		exit(1);                                                                                         \
	} while (0)


static inline void free_harness(struct test_harness *result)
{
	for (int i = 0; i < result->ntests; ++i) {
		if (result->outcomes[i].description)
			free((void *) result->outcomes[i].description);
		if (result->outcomes[i].reason)
			free((void *) result->outcomes[i].reason);
	}

	free(result->outcomes);
}

static inline void initialize_options(struct test_options *options)
{
	options->parallel = 1;
}

static inline void tap_emit_outcome(tap_t *tap, struct test_outcome *outcome)
{
	assert(outcome->description);

	switch (outcome->status) {
		case PASS:
			tap_ok(tap, outcome->description);
			break;
		case XFAIL:
			assert(outcome->reason);
			tap_xfail(tap, outcome->reason, outcome->description);
			break;
		case SKIP:
			assert(outcome->reason);
			tap_skip(tap, outcome->reason, outcome->description);
			break;
		case FAIL:
			tap_fail(tap, outcome->description);
			break;
		case ERROR:
			tap_error(tap, outcome->description);
			break;
		default:
			assert(0);
	}
}

void handle_plan(const char *line, struct test_harness *result, __unused enum test_status type, __unused struct test_options *options)
{
	assert(result->ntests == 0);
	sscanf(line, "pl%d\n", &result->ntests);
	printf("# Detected tests %d\n", result->ntests);
	result->curr = 0;

	result->outcomes = malloc(sizeof(struct test_outcome) * result->ntests);
}

void handle_single(const char *line, struct test_harness *result, enum test_status type, __unused struct test_options *options)
{
	assert(result->ntests > 0);
	// Copy anything other than the first two characters

	struct test_outcome *outcome = &result->outcomes[result->curr++];
	outcome->status = type;
	outcome->reason = NULL;
	outcome->description = strdup(line + 2);
}

void handle_multi(const char *line, struct test_harness *result, enum test_status type, __unused struct test_options *options)
{
	assert(result->ntests > 0);
	struct test_outcome *outcome = &result->outcomes[result->curr++];

	const char *sep = strstr(line, "####");
	assert(sep);
	size_t distance = line - sep;

	outcome->status = type;
	outcome->description = strndup(line + 2, distance - 20);
	outcome->reason = strdup(sep + 4);
}

void handle_option(const char *line, __unused struct test_harness *result, __unused enum test_status type, struct test_options *options)
{
	int option, value;
	sscanf(line, "op%d %d\n", &option, &value);

	switch (option) {
		case OPTION_PARALLEL:
			options->parallel = value;
			break;
		default:
			assert(0);
	}
}

#define NUM_HANDLERS 7

void (*handlers[NUM_HANDLERS])(const char *, struct test_harness *, enum test_status, struct test_options *) = {
	handle_single, /* PASS  */
	handle_multi,  /* XFAIL */
	handle_multi,  /* SKIP  */
	handle_single, /* FAIL  */
	handle_single, /* ERROR */
	handle_plan,   /* PLAN  */
	handle_option  /* OPTION */
};

// Ensure this follows the same order as enum test_status!
const char *str_types[NUM_HANDLERS] = {
	"pa",
	"xf",
	"sk",
	"fa",
	"bo",
	"pl",
	"op"};

void process_harness_line(const char *line, struct test_harness *result, struct test_options *options)
{
	size_t length = strnlen(line, MAX_BUFF_SIZE);
	if (length < 2)
		return;

	for (int i = 0; i < NUM_HANDLERS; ++i) {
		if (strncmp(str_types[i], line, 2) == 0) {
			handlers[i](line, result, (enum test_status) i, options);
			break;
		}
	}
}

void launch_test_proc(const char *program, struct test_proc_descriptor *descriptor)
{
	// Create a pipe to read stdout and stderr from the forked process
	int pipefd[2];
	int ret = pipe(pipefd);

	if (ret)
		report_error("Could not create unnamed pipe");

	pid_t pid = fork();

	if (pid == -1) {
		// Error
		report_error("Could not fork test driver");
	} else if (pid == 0) {
		// Child
		// Redirect output
		dup2(pipefd[1], 1);
		dup2(pipefd[1], 2);
		close(pipefd[1]);

		// Exec program
		execlp(program, program, NULL);

		report_error("Could not exec test program");
	} else {
		// Parent
		descriptor->pid = pid;
		// Close pipe write end
		close(pipefd[1]);
		descriptor->readfd = pipefd[0];
		descriptor->finished = 0;
		descriptor->off = 0;
		descriptor->size = 0;
		descriptor->output = NULL;
	}
}

int monitor_procs(int n, struct test_harness *result, struct test_proc_descriptor *procs)
{
	struct pollfd descriptors[PARALLEL_TESTS];
	memset(descriptors, 0, sizeof(descriptors));
	char buffer[MAX_BUFF_SIZE];

	for (int i = 0; i < n; ++i) {
		descriptors[i].fd = procs[i].readfd;
		descriptors[i].events = POLLIN;
	}

	int openfds = n;

	while(openfds) {
		int ready = poll(descriptors, n, -1);
		if (ready == -1)
			report_error("Poll returned error");

		for (int i = 0; i < n; ++i) {
			short revents = descriptors[i].revents;
			if (revents) {
				if (revents & POLLIN) {
					// Data to read.
					int ret = read(descriptors[i].fd, buffer, MAX_BUFF_SIZE);
					assert(ret > 0);

					// Do we have space left in the process buffer to save this?
					struct test_proc_descriptor *proc = &procs[i];
					// Leave always at least one extra char at the end to place the null termination
					if (proc->size - proc->off < (ret + 1)) {
						if (proc->size == 0)
							proc->size = MAX_BUFF_SIZE;

						proc->output = realloc(proc->output, proc->size * 2);
						proc->size *= 2;
					}

					memcpy(&proc->output[proc->off], buffer, ret);
					proc->off += ret;
					proc->output[proc->off] = '\0';
				} else {
					// POLLHUP
					assert(revents & POLLHUP);
					// Close fd, reap process
					close(descriptors[i].fd);
					// Notify poll to ignore subsequent polls of this FD
					descriptors[i].fd = -1;
					int status;
					pid_t ret = waitpid(procs[i].pid, &status, 0);
					procs[i].finished = 1;
					openfds--;

					if (ret == -1)
						report_error("Waitpid");

					if(WIFEXITED(status) && WEXITSTATUS(status) == 0) {
						// All ok.
						result[i].retval = 0;
					} else {
						// The process crashed. We cannot continue
						if (WIFEXITED(status))
							return WEXITSTATUS(status);
						else
							return WTERMSIG(status);
					}
				}
			}
		}
	}

	return 0;
}

void parse_proc_outputs(int n, struct test_harness *result, struct test_options *options, struct test_proc_descriptor *procs)
{
	for (int i = 0; i < n; ++i) {
		assert(procs[i].output);

		char *line = strtok(procs[i].output, "\n");
		while (line) {
			process_harness_line(line, &result[i], options);
			line = strtok(NULL, "\n");
		}

		free(procs[i].output);
	}
}

void execute_tests(tap_t *tap, const char *program, int n, struct test_options *options)
{
	assert(n > 0);
	assert(n <= PARALLEL_TESTS);
	struct test_harness result[PARALLEL_TESTS];
	struct test_proc_descriptor *procs;

	procs = malloc(sizeof(struct test_proc_descriptor) * PARALLEL_TESTS);

	// Silence compiler warning
	result[0].ntests = 0;

	for (int i = 0; i < n; ++i) {
		result[i].ntests = 0;
		launch_test_proc(program, &procs[i]);
	}

	// Now that all tests are lauched, we have to monitor the procs until (1) they all end or (2) one crashes
	int ret = monitor_procs(n, result, procs);

	if (ret) {
		// Some process crashed. Emulate the crash in the driver.
		exit(ret);
	}

	// Parse the recorded outputs
	parse_proc_outputs(n, result, options, procs);

	// Handle test results accordingly
	// Check if any test has returned with an error
	// Similarly, check if in any case there is a mismatch in test number
	int ntests = result[0].ntests;
	for (int i = 0; i < n; ++i) {
		if (result[i].ntests != ntests) {
			tap_error(tap, "Test number mismatch");
			return;
		}
	}

	// For each test, we emit the "worst" outcome, which has the higher status number
	for (int t = 0; t < ntests; ++t) {
		struct test_outcome *max_outcome = &result[0].outcomes[t];
		for (int i = 1; i < n; ++i) {
			if (result[i].outcomes[t].status > max_outcome->status)
				max_outcome = &result[i].outcomes[t];
		}

		tap_emit_outcome(tap, max_outcome);
	}

	for (int i = 0; i < n; ++i) {
		if (result[i].ntests > 0)
			free_harness(&result[i]);
	}

	free(procs);
}

int main(int argc, char *argv[])
{
	if (argc != 2)
		usage(argv[0]);

	tap_t tap;
	tap_init(&tap);

	struct test_options options;
	initialize_options(&options);

	// First, execute 1 test and emit its results
	execute_tests(&tap, argv[1], 1, &options);

	if (options.parallel) {
		// Then do so for parallel testing
		execute_tests(&tap, argv[1], PARALLEL_TESTS, &options);
	}

	tap_end(&tap);
}
