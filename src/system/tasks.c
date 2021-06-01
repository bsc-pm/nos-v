/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include "system/tasks.h"

#include <sys/errno.h>

/* Create a task type with certain run/end callbacks and a label */
__public int nosv_type_init(
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

	return 0;
}

/* May return -ENOMEM. 0 on success */
/* Callable from everywhere */
__public int nosv_create(
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

	return 0;
}

/* Callable from everywhere */
__public int nosv_submit(
	nosv_task_t task,
	nosv_flags_t flags)
{
	if (unlikely(!task))
		return -EINVAL;

	return 0;
}

/* Blocking, yield operation */
/* Callable from a task context ONLY */
__public int nosv_pause(
	nosv_flags_t flags)
{
	return 0;
}

/* Deadline tasks */
__public int nosv_waitfor(
	uint64_t ns)
{
	return 0;
}

/* Callable from everywhere */
__public int nosv_destroy(
	nosv_task_t task,
	nosv_flags_t flags)
{
	if (unlikely(!task))
		return -EINVAL;

	return 0;
}
