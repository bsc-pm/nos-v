/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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

struct harness_args args_threads[PARALLEL_TESTS];

void usage(const char *program)
{
	printf("Usage: driver %s <test-to-execute>\n", program);
	exit(1);
}

static inline void free_harness(struct test_harness *result)
{
	for (int i = 0; i < result->ntests; ++i) {
		if (result->outcomes[i].description)
			free((void *)result->outcomes[i].description);
		if (result->outcomes[i].reason)
			free((void *)result->outcomes[i].reason);
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

	switch(option) {
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
			handlers[i](line, result, (enum test_status)i, options);
			break;
		}
	}
}

static inline void *__entry_execute_tests(void *arg)
{
	struct harness_args *args = (struct harness_args *)arg;
	char buffer[MAX_BUFF_SIZE];
	FILE *fp;

	fp = popen(args->program, "r");
	if (fp == NULL) {
		fprintf(stderr, "Cannot open program\n");
		exit(1);
	}

	while (fgets(buffer, MAX_BUFF_SIZE, fp) != NULL) {
		process_harness_line(buffer, args->result, args->options);
	}

	args->result->retval = pclose(fp);

	return NULL;
}

void execute_tests(tap_t *tap, const char *program, int n, struct test_options *options)
{
	assert(n > 0);
	struct test_harness result[PARALLEL_TESTS];
	pthread_t threads[PARALLEL_TESTS];

	// Silence compiler warning
	result[0].ntests = 0;

	for (int i = 0; i < n; ++i) {
		result[i].ntests = 0;
		args_threads[i].program = program;
		args_threads[i].options = options;
		args_threads[i].result = &result[i];
		pthread_create(&threads[i], NULL, __entry_execute_tests, &args_threads[i]);
	}

	for (int i = 0; i < n; ++i) {
		pthread_join(threads[i], NULL);
	}

	// Handle test results accordingly
	// Check if any test has returned with an error
	// Similarly, check if in any case there is a mismatch in test number
	int ntests = result[0].ntests;
	for (int i = 0; i < n; ++i) {
		if (result[i].retval)
			exit(result[i].retval);

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
}

int main(int argc, char *argv[])
{
	if (argc != 2)
		usage(argv[0]);

	// Clean up shared memory. Ignore return value.
	unlink("/dev/shm/nosv");

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

	// Check if shared memory exists after the test
	struct stat buf;
	int ret = stat("/dev/shm/nosv", &buf);
	if (ret == 0 || errno != ENOENT) {
		tap_fail(&tap, "Did not clean shared memory");
	}

	tap_end(&tap);
}
