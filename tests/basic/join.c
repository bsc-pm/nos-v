/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2025-2026 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <nosv.h>

#include "test.h"
#include "common/utils.h"


nosv_task_t task_attach;
test_t t;


nosv_task_t global_task;

atomic_int completed = 0;

void run_delay(nosv_task_t task)
{
	CHECK(nosv_waitfor(100000000, NULL)); //100ms
	CHECK(nosv_set_result((void *)1234));
}

void run_delay_join(nosv_task_t task)
{
	CHECK(nosv_waitfor(1000000, NULL)); //1ms
	CHECK(nosv_wait(global_task, 0) != NOSV_ERR_INVALID_PARAMETER);
	CHECK(nosv_wait(global_task, 100) != NOSV_ERR_INVALID_PARAMETER);
	CHECK(nosv_wait(global_task, -1) != NOSV_ERR_INVALID_PARAMETER);
}

void complete(nosv_task_t task)
{
	atomic_fetch_add(&completed, 1);
}

int main()
{
	nosv_task_t task1, task2;
	nosv_task_type_t task_type1, task_type2;
	void *res = NULL;
	const int ntasks = 128;
	nosv_task_t tasks[ntasks];
	void *results[ntasks];
	test_init(&t, 11);

	CHECK(nosv_init());
	CHECK(nosv_attach(&task_attach, /* affinity */ NULL, NULL, NOSV_ATTACH_NONE));

	// Now we are inside nOS-V

	CHECK(nosv_type_init(&task_type1, &run_delay, NULL, &complete, NULL, NULL, NULL, NOSV_TYPE_INIT_NONE));
	CHECK(nosv_type_init(&task_type2, &run_delay_join, NULL, &complete, NULL, NULL, NULL, NOSV_TYPE_INIT_NONE));

	// Check join
	CHECK(nosv_create(&task1, task_type1, sizeof(int), NOSV_CREATE_JOINABLE));
	CHECK(nosv_submit(task1, NOSV_SUBMIT_NONE));
	CHECK(nosv_join(task1, &res, -1));

	test_check(&t, (atomic_load(&completed) == 1) && ((uint64_t)res == 1234), "Join OK");

	// Check join and timeout
	CHECK(nosv_create(&task1, task_type1, sizeof(int), NOSV_CREATE_JOINABLE));
	CHECK(nosv_submit(task1, NOSV_SUBMIT_NONE));
	CHECK(nosv_join(task1, NULL, 100) != NOSV_ERR_TIMEOUT); // Timeout
	CHECK(nosv_join(task1, NULL, 10000000000)); //10s

	test_check(&t, atomic_load(&completed) == 2, "Join timeout OK");

	// Check wait
	CHECK(nosv_create(&task1, task_type1, sizeof(int), NOSV_CREATE_JOINABLE));
	CHECK(nosv_submit(task1, NOSV_SUBMIT_NONE));
	CHECK(nosv_wait(task1, -1));
	CHECK(nosv_destroy(task1, NOSV_DESTROY_NONE));

	test_check(&t, atomic_load(&completed) == 3, "Wait OK");

	// Check wait and timeout
	CHECK(nosv_create(&task1, task_type1, sizeof(int), NOSV_CREATE_JOINABLE));
	CHECK(nosv_submit(task1, NOSV_SUBMIT_NONE));
	CHECK(nosv_wait(task1, 100) != NOSV_ERR_TIMEOUT); // Timeout
	CHECK(nosv_wait(task1, 10000000000)); //10s
	CHECK(nosv_destroy(task1, NOSV_DESTROY_NONE));

	test_check(&t, atomic_load(&completed) == 4, "Wait timeout OK");

	// Check join_all
	for (int i = 0; i < ntasks; i++)
		CHECK(nosv_create(&tasks[i], task_type1, sizeof(int), NOSV_CREATE_JOINABLE));
	for (int i = 0; i < ntasks; i++)
		CHECK(nosv_submit(tasks[i], NOSV_SUBMIT_NONE));
	CHECK(nosv_join_all(tasks, results, ntasks, -1));

	int failed = 0;
	for (int i = 0; i < ntasks; i++) {
		if((uint64_t)results[i] != 1234) {
			failed = 1;
			break;
		}
	}
	if (failed)
		test_fail(&t, "join_all: Incorrect results");
	else
		test_check(&t, atomic_load(&completed) == (4 + ntasks), "join_all OK");

	// Check join_all and timeout
	for (int i = 0; i < ntasks; i++)
		CHECK(nosv_create(&tasks[i], task_type1, sizeof(int), NOSV_CREATE_JOINABLE));
	for (int i = 0; i < ntasks; i++)
		CHECK(nosv_submit(tasks[i], NOSV_SUBMIT_NONE));
	CHECK(nosv_join_all(tasks, NULL, ntasks, 100) != NOSV_ERR_TIMEOUT); //Timeout
	CHECK(nosv_join_all(tasks, NULL, ntasks, 10000000000)); //10s

	test_check(&t, atomic_load(&completed) == (4 + 2*ntasks), "join_all timeout OK");

	// Check wait_all
	for (int i = 0; i < ntasks; i++)
		CHECK(nosv_create(&tasks[i], task_type1, sizeof(int), NOSV_CREATE_JOINABLE));
	for (int i = 0; i < ntasks; i++)
		CHECK(nosv_submit(tasks[i], NOSV_SUBMIT_NONE));
	CHECK(nosv_wait_all(tasks, ntasks, -1));
	for (int i = 0; i < ntasks; i++)
		CHECK(nosv_destroy(tasks[i], NOSV_DESTROY_NONE));

	test_check(&t, atomic_load(&completed) == (4 + 3*ntasks), "wait_all OK");

	// Check wait_all and timeout
	for (int i = 0; i < ntasks; i++)
		CHECK(nosv_create(&tasks[i], task_type1, sizeof(int), NOSV_CREATE_JOINABLE));
	for (int i = 0; i < ntasks; i++)
		CHECK(nosv_submit(tasks[i], NOSV_SUBMIT_NONE));
	CHECK(nosv_wait_all(tasks, ntasks, 100) != NOSV_ERR_TIMEOUT); //Timeout
	CHECK(nosv_wait_all(tasks, ntasks, 10000000000)); //10s
	for (int i = 0; i < ntasks; i++)
		CHECK(nosv_destroy(tasks[i], NOSV_DESTROY_NONE));

	test_check(&t, atomic_load(&completed) == (4 + 4*ntasks), "wait_all timeout OK");

	// Check try_join
	CHECK(nosv_create(&task1, task_type1, sizeof(int), NOSV_CREATE_JOINABLE));
	CHECK(nosv_submit(task1, NOSV_SUBMIT_NONE));
	int joined = 0;
	do {
		nosv_waitfor(1000000, NULL); //1ms
		joined = nosv_join(task1, NULL, 0);
	} while(joined == NOSV_ERR_TIMEOUT);

	test_check(&t, atomic_load(&completed) == (5 + 4*ntasks), "Try join OK");

	// Check try_wait
	CHECK(nosv_create(&task1, task_type1, sizeof(int), NOSV_CREATE_JOINABLE));
	CHECK(nosv_submit(task1, NOSV_SUBMIT_NONE));
	int waited = 0;
	do {
		nosv_waitfor(1000000, NULL); //1ms
		waited = nosv_wait(task1, 0);
	} while(waited == NOSV_ERR_TIMEOUT);
	CHECK(nosv_destroy(task1, NOSV_DESTROY_NONE));

	test_check(&t, atomic_load(&completed) == (6 + 4*ntasks), "Try wait OK");

	// Check
	CHECK(nosv_create(&global_task, task_type1, sizeof(int), NOSV_CREATE_JOINABLE));
	CHECK(nosv_create(&task2, task_type2, sizeof(int), NOSV_CREATE_JOINABLE));
	CHECK(nosv_submit(global_task, NOSV_SUBMIT_NONE));
	CHECK(nosv_submit(task2, NOSV_SUBMIT_NONE));
	CHECK(nosv_join(global_task, NULL, -1));
	CHECK(nosv_join(task2, NULL, -1));

	test_check(&t, atomic_load(&completed) == (8 + 4*ntasks), "Cannot wait for task while it is being joined");

	CHECK(nosv_type_destroy(task_type1, NOSV_TYPE_DESTROY_NONE));
	CHECK(nosv_type_destroy(task_type2, NOSV_TYPE_DESTROY_NONE));

	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());

	test_end(&t);
	return 0;
}
