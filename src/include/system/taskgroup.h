/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef TASK_GROUP_H
#define TASK_GROUP_H

#include "nosv-internal.h"

static inline void task_group_init(task_group_t *group)
{
	group->count = 0;
	group->head_task = NULL;
}

static inline int task_group_empty(task_group_t *group)
{
	return group->count == 0;
}

static inline size_t task_group_count(task_group_t *group)
{
	return group->count;
}

static inline nosv_task_t task_group_head(task_group_t *group)
{
	return group->head_task;
}

static inline void task_group_clear(task_group_t *group)
{
	task_group_init(group);
}

static inline void task_group_add(task_group_t *group, nosv_task_t task)
{
	if (group->head_task) {
		list_add_tail(&(group->head_task->list_hook), &(task->list_hook));
	} else {
		list_init(&task->list_hook);
		group->head_task = task;
	}

	group->count++;
}


#endif // TASK_GROUP_H
