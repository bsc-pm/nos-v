/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023-2025 Barcelona Supercomputing Center (BSC)
*/

#include <nosv.h>
#include <nosv/alpi.h>
#include <nosv/alpi-defs.h>

#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test.h"
#include "common/utils.h"

test_t test;

// Tests can fail after three seconds
const int timeout = 3000;

_Atomic(struct alpi_task *) blocked_task;
_Atomic(struct alpi_task *) events_task;

struct alpi_task *get_current_task(void)
{
	struct alpi_task *task;
	CHECK(alpi_task_self(&task));

	test_check(&test, task != NULL, "The current task has a valid task handle");
	return task;
}

// Perform several checks regarding the versions and features of ALPI
void perform_version_checks(void)
{
	int required[2];
	int provided[2];
	int err;

	// Version checks
	CHECK(alpi_version_get(&provided[0], &provided[1]));
	test_check(&test, provided[0] == ALPI_VERSION_MAJOR, "Same provided and requested major versions");
	test_check(&test, provided[1] == ALPI_VERSION_MINOR, "Same provided and requested minor versions");

	required[0] = ALPI_VERSION_MAJOR;
	required[1] = 0;
	err = alpi_version_check(required[0], required[1]);
	test_check(&test, !err, "Requesting same or lower minor version is valid");

	required[0] = ALPI_VERSION_MAJOR;
	required[1] = ALPI_VERSION_MINOR + 1;
	err = alpi_version_check(required[0], required[1]);
	test_check(&test, err == ALPI_ERR_VERSION, "Requesting higher minor version is invalid");

	required[0] = ALPI_VERSION_MAJOR - 1;
	required[1] = 0;
	err = alpi_version_check(required[0], required[1]);
	test_check(&test, err == ALPI_ERR_VERSION, "Requesting lower major version is invalid");

	required[0] = ALPI_VERSION_MAJOR + 1;
	required[1] = 0;
	err = alpi_version_check(required[0], required[1]);
	test_check(&test, err == ALPI_ERR_VERSION, "Requesting higher major version is invalid");


	// Runtime info checks
	int length, test_pass;
	char buffer[64];
	err = alpi_info_get(ALPI_INFO_RUNTIME_NAME, buffer, sizeof(buffer), &(length));
	test_pass = (err == ALPI_SUCCESS) && (strcmp(buffer, "nOS-V") == 0) && (length > 0);
	test_check(&test, test_pass, "Requesting the runtime name provides valid information");

	err = alpi_info_get(ALPI_INFO_RUNTIME_VENDOR, buffer, sizeof(buffer), &(length));
	test_pass = (err == ALPI_SUCCESS) && (strcmp(buffer, "STAR Team (BSC)") == 0) && (length > 0);
	test_check(&test, test_pass, "Requesting the runtime vendor provides valid information");

	char tmp_buffer[64];
	snprintf(tmp_buffer, sizeof(tmp_buffer), "ALPI %d.%d (nOS-V)",  ALPI_VERSION_MAJOR, ALPI_VERSION_MINOR);
	err = alpi_info_get(ALPI_INFO_VERSION, buffer, sizeof(buffer), &(length));
	test_pass = (err == ALPI_SUCCESS) && (strcmp(buffer, tmp_buffer) == 0);
	test_check(&test, test_pass, "Requesting the full version of ALPI and the underlying runtime provides valid information");

	err = alpi_info_get(ALPI_INFO_VERSION, buffer, sizeof(buffer), NULL);
	test_check(&test, err == ALPI_SUCCESS, "Passing a null pointer as the length parameter is valid");

	err = alpi_info_get(-1, buffer, sizeof(buffer), &(length));
	test_check(&test, err == ALPI_ERR_PARAMETER, "Passing an unknown query identifier is invalid");

	err = alpi_info_get(ALPI_INFO_RUNTIME_NAME, NULL, sizeof(buffer), &(length));
	test_check(&test, err == ALPI_ERR_PARAMETER, "Passing a null pointer as the buffer is invalid");

	err = alpi_info_get(ALPI_INFO_RUNTIME_NAME, buffer, 0, &(length));
	test_check(&test, err == ALPI_ERR_PARAMETER, "Passing a non-positive size for the buffer is invalid");


	// Feature checks
	err = alpi_feature_check((1 << 0) /* ALPI_FEATURE_BLOCKING */);
	test_check(&test, err == ALPI_SUCCESS, "ALPI feature 'Blocking' is supported");

	err = alpi_feature_check((1 << 1) /* ALPI_FEATURE_EVENTS */);
	test_check(&test, err == ALPI_SUCCESS, "ALPI feature 'Events' is supported");

	err = alpi_feature_check((1 << 2) /* ALPI_FEATURE_RESOURCES */);
	test_check(&test, err == ALPI_SUCCESS, "ALPI feature 'Resources' is supported");

	err = alpi_feature_check((1 << 3) /* ALPI_FEATURE_SUSPEND */);
	test_check(&test, err == ALPI_SUCCESS, "ALPI feature 'Suspend' is supported");

	err = alpi_feature_check((1 << 0) | (1 << 3));
	test_check(&test, err == ALPI_SUCCESS, "ALPI features 'Blocking' and 'Suspend' are supported");

	int unknown_feature = (1 << 15);
	err = alpi_feature_check(unknown_feature);
	test_check(&test, err == ALPI_ERR_FEATURE_UNKNOWN, "ALPI feature with unknown identifier is not supported");

	err = alpi_feature_check((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3));
	test_check(&test, err == ALPI_SUCCESS, "ALPI features 'Blocking', 'Events', 'Resources' and 'Suspend' are supported");

	err = alpi_feature_check((1 << 0) | (1 << 1) | unknown_feature);
	test_check(&test, err == ALPI_ERR_FEATURE_UNKNOWN, "ALPI features 'Blocking' and 'Events' are supported but unknown identifier is not");
}

// Perform several checks regarding the available cpus
void perform_cpu_checks(void)
{
	uint64_t count, logical, system;
	CHECK(alpi_cpu_count(&count));
	CHECK(alpi_cpu_logical_id(&logical));
	CHECK(alpi_cpu_system_id(&system));

	test_check(&test, count > 0, "There is at least one cpu available");
	test_check(&test, logical >= 0 && logical < count, "The logical cpu id is valid");
}

// The body of the polling task
void task_polling_body(void *args)
{
	test_check(&test, args != NULL, "Spawned task body received correct args");

	perform_cpu_checks();

	// Wait until the blocked and the events tasks have done their actions
	test_check_waitfor_api(&test, atomic_load(&blocked_task) && atomic_load(&events_task),
		timeout, alpi_task_waitfor_ns, "All spawned tasks finished");

	// Retrieve all the task handles
	struct alpi_task * to_unblock = atomic_load(&blocked_task);
	struct alpi_task * to_decrease = atomic_load(&events_task);
	struct alpi_task *task = get_current_task();

	test_check(&test, to_unblock != to_decrease, "Different tasks have different handle");
	test_check(&test, to_unblock != task, "Different tasks have different handle");
	test_check(&test, to_decrease != task, "Different tasks have different handle");

	// Mark those tasks processed by nullifying the handles
	atomic_store(&blocked_task, NULL);
	atomic_store(&events_task, NULL);

	// Unblock and decrease the events of the tasks
	CHECK(alpi_task_unblock(to_unblock));
	CHECK(alpi_task_events_decrease(to_decrease, 1));
}

// The completion callback of the spawned tasks
void task_completed(void *args)
{
	test_check(&test, args != NULL, "Spawned task completion received correct args");

	// Notify the main function that a spawned task has completed
	atomic_int *countdown = (atomic_int *) args;
	atomic_fetch_sub(countdown, 1);
}

// The body of the task that blocks
void task_blocks_body(void *args)
{
	perform_cpu_checks();

	// Notify the polling task and block the current task
	struct alpi_task *task = get_current_task();
	atomic_store(&blocked_task, task);
	CHECK(alpi_task_block(task));

	// After being resumed, check that the polling task actually processed the
	// unblocking of the task
	test_check(&test, atomic_load(&blocked_task) == NULL, "Blocked task was actually processed");
}

// The body of the task that registers events
void task_events_body(void *args)
{
	perform_cpu_checks();

	// Increase the task's events and notify the polling task
	struct alpi_task *task = get_current_task();
	CHECK(alpi_task_events_increase(task, 1));
	atomic_store(&events_task, task);
}

int main()
{
	test_init(&test, 46);

	CHECK(nosv_init());

	// Check that the main function is not executed by a task
	struct alpi_task * main_task;
	CHECK(alpi_task_self(&main_task));
	test_check(&test, main_task == NULL, "The main function is not an implicit task");

	// Attach the current thread
	nosv_task_t task;
	CHECK(nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE));

	// Check now that the main function is executed by a task
	CHECK(alpi_task_self(&main_task));
	test_check(&test, main_task != NULL, "The main function is now executed by an attached thread");

	// Perform checks regarding versions and available cpus
	perform_version_checks();
	perform_cpu_checks();

	// There will be three spawned tasks
	atomic_int countdown;
	atomic_init(&countdown, 3);
	atomic_init(&blocked_task, NULL);
	atomic_init(&events_task, NULL);

	// Spawn two tasks that will block and register events, respectively
	CHECK(alpi_task_spawn(
		task_blocks_body, NULL,
		task_completed, (void *) &countdown,
		"task blocks", NULL));

	CHECK(alpi_task_spawn(
		task_events_body, NULL,
		task_completed, (void *) &countdown,
		"task events", NULL));

	uint64_t attributes_size;
	struct alpi_attr *attributes;

	// Create an empty task attribute structure. For the moment, there is no
	// valid attribute so the runtime just creates the default attribute
	CHECK(alpi_attr_create(&attributes));
	CHECK(alpi_attr_init(attributes));
	CHECK(alpi_attr_size(&attributes_size));

	test_check(&test, attributes == NULL, "The attributes are the default");
	test_check(&test, attributes_size == 0, "The attributes have zero size");

	// Spawn a task to unblock and fulfill the events of the previous tasks
	CHECK(alpi_task_spawn(
		task_polling_body, (void *) &attributes_size,
		task_completed, (void *) &countdown,
		"task polling", attributes));

	// Now it is safe to destroy the attributes
	CHECK(alpi_attr_destroy(attributes));

	// Wait until the spawned tasks has finished
	test_check_waitfor(&test, atomic_load(&countdown) == 0, timeout, "All spawned tasks finished");

	// Detach thread and shutdown
	CHECK(nosv_detach(NOSV_DETACH_NONE));
	CHECK(nosv_shutdown());

	test_end(&test);

	return 0;
}
