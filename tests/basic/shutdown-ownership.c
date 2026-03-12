/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2026 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"

#include <nosv.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void *shutdown_thread(void *arg)
{
	(void) arg;
	int err = nosv_shutdown();
	return (void *) (intptr_t) err;
}

int main(void)
{
	test_t test;
	pthread_t pthread;
	void *retval = NULL;
	int err;

	test_init(&test, 2);

	CHECK(nosv_init());

	err = pthread_create(&pthread, NULL, shutdown_thread, NULL);
	if (err) {
		fprintf(stderr, "Error: pthread_create: %s\n", strerror(err));
		return 1;
	}

	err = pthread_join(pthread, &retval);
	if (err) {
		fprintf(stderr, "Error: pthread_join: %s\n", strerror(err));
		return 1;
	}

	test_check(&test,
		((intptr_t) retval) != NOSV_SUCCESS,
		"nosv_shutdown from a non-init thread fails");

	err = nosv_shutdown();
	test_check(&test, err == NOSV_SUCCESS, "init thread can still shutdown nOS-V");
}
