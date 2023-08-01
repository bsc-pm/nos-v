/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"

#include <nosv.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define NTHREADS 10

pthread_barrier_t barrier;

void *thread_body(void *thread_args)
{
	int err1 = nosv_init();
	pthread_barrier_wait(&barrier);
	int err2 = nosv_shutdown();

	return (void *)(intptr_t)(err1 || err2);
}

int main() {
	int rc;
	test_t test;

	test_init(&test, 1);

	pthread_barrier_init(&barrier, NULL, NTHREADS);

	pthread_t threads[NTHREADS];
	for (int t = 0; t < NTHREADS; ++t) {
		if ((rc = pthread_create(&threads[t], NULL, thread_body, NULL))) {
			fprintf(stderr, "pthread_create error %s\n", strerror(rc));
			exit(EXIT_FAILURE);
		}
	}

	void *retval;
	int errors = 0;
	for (int t = 0; t < NTHREADS; ++t) {
		if ((rc = pthread_join(threads[t], &retval))) {
			fprintf(stderr, "pthread_join error %s\n", strerror(rc));
			exit(EXIT_FAILURE);
		}
		if (retval)
			++errors;
	}

	pthread_barrier_destroy(&barrier);

	test_check(&test, !errors, "Concurrent init/shutdown work correctly");
}
