/*
	This file is part of Nanos6 and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023-2024 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>

#include "common.h"
#include "error.h"
#include "generic/clock.h"
#include "hardware/threads.h"
#include "hardware/topology.h"
#include "nosv.h"
#include "nosv-internal.h"
#include "nosv/alpi.h"

static const char *errors[ALPI_ERR_MAX] = {
	[ALPI_SUCCESS] =             "Operation succeeded",
	[ALPI_ERR_VERSION] =         "Incompatible version",
	[ALPI_ERR_NOT_INITIALIZED] = "Runtime system not initialized",
	[ALPI_ERR_PARAMETER] =       "Invalid parameter",
	[ALPI_ERR_OUT_OF_MEMORY] =   "Failed to allocate memory",
	[ALPI_ERR_OUTSIDE_TASK] =    "Must run within a task",
	[ALPI_ERR_UNKNOWN] =         "Unknown error",
};

static int errors_mapping[-NOSV_ERR_MAX] = {
	[-NOSV_SUCCESS] =                   ALPI_SUCCESS,
	[-NOSV_ERR_INVALID_CALLBACK] =      ALPI_ERR_UNKNOWN,
	[-NOSV_ERR_INVALID_METADATA_SIZE] = ALPI_ERR_UNKNOWN,
	[-NOSV_ERR_INVALID_OPERATION] =     ALPI_ERR_UNKNOWN,
	[-NOSV_ERR_INVALID_PARAMETER] =     ALPI_ERR_PARAMETER,
	[-NOSV_ERR_NOT_INITIALIZED] =       ALPI_ERR_NOT_INITIALIZED,
	[-NOSV_ERR_OUT_OF_MEMORY] =         ALPI_ERR_OUT_OF_MEMORY,
	[-NOSV_ERR_OUTSIDE_TASK] =          ALPI_ERR_OUTSIDE_TASK,
	[-NOSV_ERR_UNKNOWN] =               ALPI_ERR_UNKNOWN,
};

static inline int translate_error(int error_code)
{
	if (error_code > 0 || error_code <= NOSV_ERR_MAX)
		return ALPI_ERR_UNKNOWN;

	return errors_mapping[-error_code];
}

const char *alpi_error_string(int err)
{
	if (err < 0 || err >= ALPI_ERR_MAX)
		return "Error code not recognized";

	assert(errors[err]);
	return errors[err];
}

int alpi_version_check(int major, int minor)
{
	if (major != ALPI_VERSION_MAJOR)
		return ALPI_ERR_VERSION;
	if (minor > ALPI_VERSION_MINOR)
		return ALPI_ERR_VERSION;
	return 0;
}

int alpi_version_get(int *major, int *minor)
{
	if (!major || !minor)
		return ALPI_ERR_PARAMETER;

	*major = ALPI_VERSION_MAJOR;
	*minor = ALPI_VERSION_MINOR;
	return 0;
}

int alpi_task_self(struct alpi_task **handle)
{
	if (!handle)
		return ALPI_ERR_PARAMETER;

	*handle = (struct alpi_task *) nosv_self();
	return 0;
}

int alpi_task_block(struct alpi_task *handle)
{
	nosv_task_t task = worker_current_task();
	if (!task)
		return ALPI_ERR_OUTSIDE_TASK;
	if (task != (nosv_task_t) handle)
		return ALPI_ERR_PARAMETER;

	int err = nosv_pause(NOSV_PAUSE_NONE);
	if (err)
		return translate_error(err);

	return 0;
}

int alpi_task_unblock(struct alpi_task *handle)
{
	nosv_task_t task = (nosv_task_t) handle;
	if (!task)
		return ALPI_ERR_PARAMETER;

	int err = nosv_submit(task, NOSV_SUBMIT_UNLOCKED);
	if (err)
		return translate_error(err);

	return 0;
}

int alpi_task_events_increase(struct alpi_task *handle, uint64_t increment)
{
	nosv_task_t task = worker_current_task();
	if (!task)
		return ALPI_ERR_OUTSIDE_TASK;
	if (task != (nosv_task_t) handle)
		return ALPI_ERR_PARAMETER;
	if (increment == 0)
		return 0;

	int err = nosv_increase_event_counter(increment);
	if (err)
		return translate_error(err);

	return 0;
}

int alpi_task_events_test(struct alpi_task *handle, uint64_t *has_events)
{
	nosv_task_t task = worker_current_task();
	if (!task)
		return ALPI_ERR_OUTSIDE_TASK;
	if (task != (nosv_task_t) handle)
		return ALPI_ERR_PARAMETER;
	if (!has_events)
		return ALPI_ERR_PARAMETER;

	*has_events = nosv_has_events();

	return 0;
}

int alpi_task_events_decrease(struct alpi_task *handle, uint64_t decrement)
{
	nosv_task_t task = (nosv_task_t) handle;
	if (!task)
		return ALPI_ERR_PARAMETER;
	if (decrement == 0)
		return 0;

	int err = nosv_decrease_event_counter(task, decrement);
	if (err)
		return translate_error(err);

	return 0;
}

int alpi_task_waitfor_ns(uint64_t target_ns, uint64_t *actual_ns)
{
	int err;
	if (target_ns == 0) {
		// Perform a scheduling point if required
		uint64_t start_ns;
		if (actual_ns)
			start_ns = clock_ns();
		err = nosv_schedpoint(NOSV_SCHEDPOINT_NONE);
		if (actual_ns)
			*actual_ns = clock_ns() - start_ns;
	} else {
		err = nosv_waitfor(target_ns, actual_ns);
	}

	if (err)
		return translate_error(err);

	return 0;
}

int alpi_attr_create(struct alpi_attr **attr)
{
	if (!attr)
		return ALPI_ERR_PARAMETER;

	*attr = NULL;
	return 0;
}

int alpi_attr_destroy(__maybe_unused struct alpi_attr *attr)
{
	return 0;
}

int alpi_attr_init(__maybe_unused struct alpi_attr *attr)
{
	return 0;
}

int alpi_attr_size(uint64_t *attr_size)
{
	if (!attr_size)
		return ALPI_ERR_PARAMETER;

	*attr_size = 0;
	return 0;
}

struct spawn_desc {
	void (*body)(void *);
	void *body_args;
	void (*completion_callback)(void *);
	void *completion_args;
};

static inline void spawn_run(nosv_task_t task)
{
	struct spawn_desc *desc = (struct spawn_desc *) nosv_get_task_metadata(task);
	assert(desc);

	desc->body(desc->body_args);
}

static inline void spawn_completed(nosv_task_t task)
{
	struct spawn_desc *desc = (struct spawn_desc *) nosv_get_task_metadata(task);
	assert(desc);

	desc->completion_callback(desc->completion_args);

	nosv_task_type_t type = nosv_get_task_type(task);

	int err = nosv_destroy(task, NOSV_DESTROY_NONE);
	if (err)
		nosv_abort("Error destroying spawned task: %s", nosv_get_error_string(err));

	err = nosv_type_destroy(type, NOSV_TYPE_DESTROY_NONE);
	if (err)
		nosv_abort("Error destroying spawned task type: %s", nosv_get_error_string(err));
}

int alpi_task_spawn(
	void (*body)(void *),
	void *body_args,
	void (*completion_callback)(void *),
	void *completion_args,
	const char *label,
	__maybe_unused const struct alpi_attr *attr)
{
	if (!body || !completion_callback)
		return ALPI_ERR_PARAMETER;

	nosv_task_t task = NULL;
	nosv_task_type_t type = NULL;

	int err = nosv_type_init(&type, spawn_run, NULL, spawn_completed, label, NULL, NULL, NOSV_TYPE_INIT_NONE);
	if (err)
		goto fail;

	err = nosv_create(&task, type, sizeof(struct spawn_desc), NOSV_CREATE_NONE);
	if (err)
		goto fail;

	struct spawn_desc *desc = (struct spawn_desc *) nosv_get_task_metadata(task);
	assert(desc);

	desc->body = body;
	desc->body_args = body_args;
	desc->completion_callback = completion_callback;
	desc->completion_args = completion_args;

	err = nosv_submit(task, NOSV_SUBMIT_NONE);
	if (!err)
		return 0;

fail:
	if (task)
		nosv_destroy(task, NOSV_DESTROY_NONE);
	if (type)
		nosv_type_destroy(type, NOSV_TYPE_DESTROY_NONE);

	return translate_error(err);
}

int alpi_cpu_count(uint64_t *count)
{
	if (!count)
		return ALPI_ERR_PARAMETER;

	int ret = nosv_get_num_cpus();
	if (ret < 0)
		return translate_error(ret);

	*count = ret;
	return 0;
}

int alpi_cpu_logical_id(uint64_t *logical_id)
{
	if (!logical_id)
		return ALPI_ERR_PARAMETER;

	int ret = nosv_get_current_logical_cpu();
	if (ret < 0)
		return translate_error(ret);

	*logical_id = ret;
	return 0;
}

int alpi_cpu_system_id(uint64_t *system_id)
{
	if (!system_id)
		return ALPI_ERR_PARAMETER;

	int ret = nosv_get_current_system_cpu();
	if (ret < 0)
		return translate_error(ret);

	*system_id = ret;
	return 0;
}

int alpi_task_suspend_mode_set(struct alpi_task *handle, alpi_suspend_mode_t suspend_mode, uint64_t args)
{
	nosv_task_t task = worker_current_task();
	if (!task)
		return ALPI_ERR_OUTSIDE_TASK;
	if (task != (nosv_task_t) handle)
		return ALPI_ERR_PARAMETER;

	int err = nosv_set_suspend_mode((nosv_suspend_mode_t) suspend_mode, args);
	if (err)
		return translate_error(err);
	return 0;
}

int alpi_task_suspend(struct alpi_task *handle)
{
	nosv_task_t task = worker_current_task();
	if (!task)
		return ALPI_ERR_OUTSIDE_TASK;
	if (task != (nosv_task_t) handle)
		return ALPI_ERR_PARAMETER;

	int err = nosv_suspend();
	if (err)
		return translate_error(err);
	return 0;
}
