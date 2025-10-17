/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2025 Barcelona Supercomputing Center (BSC)
*/

#include <nosv.h>
#include <nosv/compat.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#include "test.h"
#include "common/utils.h"

#define NREADERS 64
#define NWRITERS 8

#define ONE_MS 1000ULL

test_t test;
nosv_task_type_t type_reader;
nosv_task_type_t type_writer;
nosv_task_t readers[NREADERS];
nosv_task_t writers[NWRITERS];
nosv_rwlock_t rwlock = NOSV_RWLOCK_INITIALIZER;

atomic_int concurrent_readers = 0;
atomic_int concurrent_writers = 0;
atomic_int concurrent_waiting_writers = 0;
atomic_int comp_rd = 0;
atomic_int comp_wr = 0;
atomic_int flag_1 = 0;
atomic_int flag_2 = 0;

void task_reader_run(nosv_task_t task)
{
	if (rand() % 2) {
		CHECK(nosv_rwlock_rdlock(&rwlock));
	} else {
		int err = EBUSY;

		while (err != 0) {
			err = nosv_rwlock_tryrdlock(&rwlock);
			if (err != EBUSY && err != 0)
				test_fail(&test, "Tryrdlock returned error: %s", nosv_get_error_string(err));
			if (err != 0)
				nosv_yield(NOSV_YIELD_NONE);
		}
	}

	atomic_fetch_add(&concurrent_readers, 1);

	while (atomic_load(&flag_1) != 1)
		CHECK(nosv_yield(NOSV_YIELD_NONE));

	test_check(&test, atomic_load(&concurrent_writers) == 0, "No writers present in critical region");

	atomic_fetch_sub(&concurrent_readers, 1);

	CHECK(nosv_rwlock_unlock(&rwlock));
}

void task_writer_run(nosv_task_t task)
{
	atomic_fetch_add(&concurrent_waiting_writers, 1);
	CHECK(nosv_rwlock_wrlock(&rwlock));
	atomic_fetch_add(&concurrent_waiting_writers, -1);

	int nwriters = atomic_fetch_add(&concurrent_writers, 1) + 1;
	test_check(&test, nwriters == 1, "Only one writer present in critical region");
	test_check(&test, atomic_load(&concurrent_readers) == 0, "No readers during write lock");

	while (atomic_load(&flag_2) != 1)
		CHECK(nosv_yield(NOSV_YIELD_NONE));

	atomic_fetch_sub(&concurrent_writers, 1);

	CHECK(nosv_rwlock_unlock(&rwlock));
}

void task_comp_rd(nosv_task_t task)
{
	atomic_fetch_add(&comp_rd, 1);
}

void task_comp_wr(nosv_task_t task)
{
	atomic_fetch_add(&comp_wr, 1);
}

void test_1(void)
{
	atomic_store(&flag_2, 1);

	// First, submit all readers and assert they arrive to the critical region.
	for (int i = 0; i < NREADERS; ++i)
		CHECK(nosv_submit(readers[i], NOSV_SUBMIT_NONE));

	test_check_waitfor(&test, atomic_load(&concurrent_readers) == NREADERS, 100 * ONE_MS, "All readers arrived at critical region");

	// Submit writers
	for (int i = 0; i < NWRITERS; ++i)
		CHECK(nosv_submit(writers[i], NOSV_SUBMIT_NONE));

	test_check(&test, atomic_load(&concurrent_writers) == 0, "No writers in critical region (main)");

	// Unlock readers
	atomic_store(&flag_1, 1);

	test_check_waitfor(&test, atomic_load(&comp_rd) == NREADERS, 100 * ONE_MS, "All readers unblocked");
	test_check_waitfor(&test, atomic_load(&comp_wr) == NWRITERS, 100 * ONE_MS, "All writers unblocked");

	// Reset
	atomic_store(&flag_1, 0);
	atomic_store(&flag_2, 0);
	atomic_store(&comp_rd, 0);
	atomic_store(&comp_wr, 0);
}

void test_2(void)
{
	// First, submit HALF of the readers and assert they arrive to the critical region.
	for (int i = 0; i < NREADERS / 2; ++i)
		CHECK(nosv_submit(readers[i], NOSV_SUBMIT_NONE));

	test_check_waitfor(&test, atomic_load(&concurrent_readers) == NREADERS / 2, 100 * ONE_MS, "Readers arrived at critical region");

	// Submit the writers
	for (int i = 0; i < NWRITERS; ++i)
		CHECK(nosv_submit(writers[i], NOSV_SUBMIT_NONE));

	test_check_waitfor(&test, atomic_load(&concurrent_waiting_writers) == NWRITERS, 100 * ONE_MS, "Writers waiting at lock");
	test_check(&test, atomic_load(&concurrent_writers) == 0, "No writers in critical region (main)");

	// Now, before unlocking the readers, we will submit the rest. Here, we should give priority to the waiting writers,
	// and thus not increase the number of readers inside the critical region

	for (int i = NREADERS / 2; i < NREADERS; ++i)
		CHECK(nosv_submit(readers[i], NOSV_SUBMIT_NONE));

	// Wait for 10 ms
	CHECK(nosv_waitfor(10 * 1000 * ONE_MS, NULL));
	test_check(&test, atomic_load(&concurrent_readers) == NREADERS / 2, "No readers infiltrated the critical region with waiting writers");
	test_check(&test, atomic_load(&concurrent_writers) == 0, "No writers in critical region (main)");

	// Unlock readers
	atomic_store(&flag_1, 1);
	test_check_waitfor(&test, atomic_load(&comp_rd) == NREADERS / 2, 100 * ONE_MS, "Half readers unblocked");
	atomic_store(&flag_1, 0);

	// Unlock writers. Priority should always be given to the writers.
	atomic_store(&flag_2, 1);
	test_check_waitfor(&test, atomic_load(&comp_wr) == NWRITERS, 100 * ONE_MS, "All writers finished");

	test_check_waitfor(&test, atomic_load(&concurrent_readers) == (NREADERS - NREADERS / 2), 100 * ONE_MS, "Rest of readers arrived at critical region");
	// Unlock readers
	atomic_store(&flag_1, 1);
	test_check_waitfor(&test, atomic_load(&comp_rd) == NREADERS, 100 * ONE_MS, "All readers finished");

	// Reset
	atomic_store(&flag_1, 0);
	atomic_store(&flag_2, 0);
	atomic_store(&comp_rd, 0);
	atomic_store(&comp_wr, 0);
}

int main()
{
	nosv_task_t task;

	test_init(&test, 2 * NREADERS + 4 * NWRITERS + 4 + 8 + 1);

	// Init nosv
	CHECK(nosv_init());
	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));
	CHECK(nosv_type_init(&type_reader, task_reader_run, NULL, task_comp_rd, "reader", NULL, NULL, NOSV_TYPE_INIT_NONE));
	CHECK(nosv_type_init(&type_writer, task_writer_run, NULL, task_comp_wr, "writer", NULL, NULL, NOSV_TYPE_INIT_NONE));

	srand(42);

	// The goal is to check that we can achieve all concurrent readers without achieving any concurrent writers.
	// Then, we will ensure that no readers can enter the region when writers are present

	for (int i = 0; i < NREADERS; ++i)
		CHECK(nosv_create(&readers[i], type_reader, 0, NOSV_CREATE_NONE));

	for (int i = 0; i < NWRITERS; ++i)
		CHECK(nosv_create(&writers[i], type_writer, 0, NOSV_CREATE_NONE));

	test_1();

	test_2();

	for (int i = 0; i < NREADERS; ++i)
		CHECK(nosv_destroy(readers[i], NOSV_DESTROY_NONE));

	for (int i = 0; i < NWRITERS; ++i)
		CHECK(nosv_destroy(writers[i], NOSV_DESTROY_NONE));

	// Shutdown nosv
	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());

	return 0;
}
