/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023-2024 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"

#include <nosv.h>
#include <string.h>

test_t test;

void *thread(void *arg)
{
	int err;
	nosv_task_t task;
	intptr_t attach = (intptr_t) arg;

	if (attach)
		CHECK(nosv_attach(&task, NULL, "thread", NOSV_ATTACH_NONE));

	err = nosv_init();
	test_check(&test, !err, "Valid nosv_init (1): initialize");

	err = nosv_init();
	test_check(&test, !err, "Valid nosv_init (1): initialize");

	err = nosv_shutdown();
	test_check(&test, !err, "Valid nosv_shutdown (1)");

	err = nosv_shutdown();
	test_check(&test, !err, "Valid nosv_shutdown (1)");

	if (attach)
		CHECK(nosv_detach(NOSV_DETACH_NONE));

	return NULL;
}

int main()
{
	int err;
	pthread_t pthread;

	test_init(&test, 16);

	err = nosv_init();
	test_check(&test, !err, "Valid nosv_init (1): initialize");

	err = nosv_init();
	test_check(&test, !err, "Valid nosv_init (2)");

	err = pthread_create(&pthread, NULL, thread, NULL);
	if (err) {
		fprintf(stderr, "Error: pthread_create: %s\n", strerror(err));
		return 1;
	}
	err = pthread_join(pthread, NULL);
	if (err) {
		fprintf(stderr, "Error: pthread_join: %s\n", strerror(err));
		return 1;
	}

	err = pthread_create(&pthread, NULL, thread, (void *) 1);
	if (err) {
		fprintf(stderr, "Error: pthread_create: %s\n", strerror(err));
		return 1;
	}
	err = pthread_join(pthread, NULL);
	if (err) {
		fprintf(stderr, "Error: pthread_join: %s\n", strerror(err));
		return 1;
	}

	err = nosv_shutdown();
	test_check(&test, !err, "Valid nosv_shutdown (1)");

	err = nosv_init();
	test_check(&test, !err, "Valid nosv_init (3)");

	err = nosv_shutdown();
	test_check(&test, !err, "Valid nosv_shutdown (2)");

	err = nosv_shutdown();
	test_check(&test, !err, "Valid nosv_shutdown (3): finalize");

	err = nosv_shutdown();
	test_check(&test, err, "Invalid nosv_shutdown (4), unmatching");

	err = nosv_init();
	test_check(&test, err, "Invalid nosv_init (5): re-initialize not allowed");
}
