/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_INTERNAL_H
#define NOSV_INTERNAL_H

#include "nosv.h"
#include "generic/list.h"

struct nosv_worker;

struct nosv_task_type
{
	nosv_task_run_callback_t run_callback;
	nosv_task_end_callback_t end_callback;
	nosv_task_event_callback_t event_callback;
	void *metadata;
	const char *label;
	int pid;
};


struct nosv_task
{
	size_t metadata;
	struct nosv_task_type *type;
	struct nosv_worker *worker;
	list_head_t list_hook;
	int priority;
};

#endif // NOSV_INTERNAL_H