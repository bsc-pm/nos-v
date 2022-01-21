/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2022 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

#include "test.h"
#include "unit.h"
#include "scheduler/dtlock.h"

// Mock salloc/sfree
void *salloc(size_t size, int cpu)
{
	return malloc(size);
}

void sfree(void *ptr, size_t size, int cpu)
{
	free(ptr);
}

struct dtlock_test_fixture {
	int cpus;
	delegation_lock_t dtlock;
	pthread_t *threads;
	volatile int is_lock_empty;
};

struct thread_arg_dtlock {
	struct dtlock_test_fixture *fixture;
	int cpu;
};

static void initialize_dtlock_fixture(struct dtlock_test_fixture *fixture)
{
	fixture->cpus = test_get_cpus();
	dtlock_init(&fixture->dtlock, fixture->cpus * 2);
	fixture->threads = (pthread_t *) malloc(sizeof(pthread_t) * fixture->cpus);
	assert(fixture->threads);
	fixture->is_lock_empty = 1;
}

static void *dtlock_check_delegation_serving_routine(void *arg)
{
	struct thread_arg_dtlock *args = (struct thread_arg_dtlock *) arg;
	int fail = 0;

	for (int i = 0; i < 1000000; ++i) {
		void *item = NULL;
		// Do different things each iteration to test everything
		switch (i % 3) {
			case 0:
				// Delegate
				int r = dtlock_lock_or_delegate(&args->fixture->dtlock, (uint64_t) args->cpu, &item, 1);

				if (r) {
					// Delegated
					if (item == NULL)
						fail = 1;
				} else {
					if (!args->fixture->is_lock_empty)
						fail = 1;

					args->fixture->is_lock_empty = 0;

					// Serve some amount of waiters
					for (int j = 0; j < 2; ++j) {
						if (!dtlock_empty(&args->fixture->dtlock)) {
							uint64_t cpu = dtlock_front(&args->fixture->dtlock);

							// Place waiting CPU onto secondary queue
							dtlock_popfront_wait(&args->fixture->dtlock, cpu);

							// Serve something. Randomly send to sleep
							if (j) {
								dtlock_serve(&args->fixture->dtlock, cpu, (void *) 1, DTLOCK_SIGNAL_DEFAULT);
							} else {
								dtlock_serve(&args->fixture->dtlock, cpu, (void *) 1, DTLOCK_SIGNAL_SLEEP);
								dtlock_serve(&args->fixture->dtlock, cpu, (void *) 1, DTLOCK_SIGNAL_WAKE);
							}
						}
					}

					args->fixture->is_lock_empty = 1;

					dtlock_unlock(&args->fixture->dtlock);
				}
				break;
			case 1:
				// Grab the lock directly
				dtlock_lock(&args->fixture->dtlock);
				if (!args->fixture->is_lock_empty)
					fail = 1;
				dtlock_unlock(&args->fixture->dtlock);
				break;
			case 2:
				// Try to grab the lock
				if (dtlock_try_lock(&args->fixture->dtlock)) {
					if (!args->fixture->is_lock_empty)
						fail = 1;

					dtlock_unlock(&args->fixture->dtlock);
				}
				break;
		}
	}

	free(args);
	pthread_exit((void *) (intptr_t) fail);
}

TEST_CASE(dtlock_check_mixed_locking)
{
	struct dtlock_test_fixture fixture;
	initialize_dtlock_fixture(&fixture);

	// We'll init one thread per CPU
	// Each thread will enter, server some of its colleagues if there are any, and then leave
	// We will need to ensure that there is never more than one server, and that
	// we serve exactly the amount expected

	for (int i = 0; i < fixture.cpus; ++i) {
		struct thread_arg_dtlock *args = malloc(sizeof(struct thread_arg_dtlock));
		assert(args);
		args->fixture = &fixture;
		args->cpu = i;

		int ret = pthread_create(&fixture.threads[i], NULL, dtlock_check_delegation_serving_routine, args);
		EXPECT_FALSE(ret);
	}

	for (int i = 0; i < fixture.cpus; ++i) {
		void *retval;
		int ret = pthread_join(fixture.threads[i], &retval);
		EXPECT_FALSE(ret);
		EXPECT_FALSE(retval);
	}

	EXPECT_TRUE(1);
}
