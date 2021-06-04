/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include "nosv-internal.h"
#include "hardware/cpus.h"
#include "hardware/threads.h"
#include "hardware/pids.h"
#include "memory/slab.h"
#include "scheduler/scheduler.h"
#include "system/tasks.h"

#include <sys/errno.h>

/* Create a task type with certain run/end callbacks and a label */
int nosv_type_init(
	nosv_task_type_t *type /* out */,
	nosv_task_run_callback_t run_callback,
	nosv_task_end_callback_t end_callback,
	nosv_task_event_callback_t event_callback,
	const char *label,
	void *metadata,
	nosv_flags_t flags)
{
	if (unlikely(!type))
		return -EINVAL;

	if (unlikely(!run_callback))
		return -EINVAL;

	nosv_task_type_t res = salloc(sizeof(struct nosv_task_type), cpu_get_current());

	if (!res)
		return -ENOMEM;

	res->run_callback = run_callback;
	res->end_callback = end_callback;
	res->event_callback = event_callback;
	res->label = label;
	res->metadata = metadata;
	res->pid = logical_pid;

	*type = res;
	return 0;
}

/* Getters and setters */
nosv_task_run_callback_t nosv_get_task_type_run_callback(nosv_task_type_t type)
{
	return type->run_callback;
}

nosv_task_end_callback_t nosv_get_task_type_end_callback(nosv_task_type_t type)
{
	return type->end_callback;
}

nosv_task_event_callback_t nosv_get_task_type_event_callback(nosv_task_type_t type)
{
	return type->event_callback;
}

const char *nosv_get_task_type_label(nosv_task_type_t type)
{
	return type->label;
}

// TODO Maybe it's worth it to place the metadata at the top to prevent continuous calls to nOS-V
void *nosv_get_task_type_metadata(nosv_task_type_t type)
{
	return type->metadata;
}

int nosv_type_destroy(
	nosv_task_type_t type,
	nosv_flags_t flags)
{
	sfree(type, sizeof(struct nosv_task_type), cpu_get_current());
	return 0;
}

/* May return -ENOMEM. 0 on success */
/* Callable from everywhere */
int nosv_create(
	nosv_task_t *task /* out */,
	nosv_task_type_t type,
	size_t metadata_size,
	nosv_flags_t flags)
{
	if (unlikely(!task))
		return -EINVAL;

	if (unlikely(!type))
		return -EINVAL;

	if (unlikely(metadata_size > NOSV_MAX_METADATA_SIZE))
		return -EINVAL;

	nosv_task_t res = salloc(sizeof(struct nosv_task) + metadata_size, cpu_get_current());

	if (!res)
		return -ENOMEM;

	res->type = type;
	res->metadata = metadata_size;
	res->worker = NULL;

	*task = res;

	return 0;
}

/* Getters and setters */
/* Read-only task attributes */
void *nosv_get_task_metadata(nosv_task_t task)
{
	return ((char *)task) + sizeof(struct nosv_task);
}

nosv_task_type_t nosv_get_task_type(nosv_task_t task)
{
	return task->type;
}

/* Read-write task attributes */
int nosv_get_task_priority(nosv_task_t task)
{
	return task->priority;
}

void nosv_set_task_priority(nosv_task_t task, int priority)
{
	task->priority = priority;
}

/* Callable from everywhere */
int nosv_submit(
	nosv_task_t task,
	nosv_flags_t flags)
{
	if (unlikely(!task))
		return -EINVAL;

	scheduler_submit(task);

	return 0;
}

/* Blocking, yield operation */
/* Callable from a task context ONLY */
int nosv_pause(
	nosv_flags_t flags)
{
	// We have to be inside a worker
	if (!worker_is_in_task())
		return -EINVAL;

	worker_yield();

	return 0;
}

/* Deadline tasks */
int nosv_waitfor(
	uint64_t ns)
{
	return -ENOSYS;
}

/* Callable from everywhere */
int nosv_destroy(
	nosv_task_t task,
	nosv_flags_t flags)
{
	if (unlikely(!task))
		return -EINVAL;

	sfree(task, sizeof(struct nosv_task) + task->metadata, cpu_get_current());

	return 0;
}

void task_execute(nosv_task_t task)
{
	task->type->run_callback(task);

	if (task->type->end_callback)
		task->type->end_callback(task);

	if (task->type->event_callback)
		task->type->event_callback(task);
}