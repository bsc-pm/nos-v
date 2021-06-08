/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_INTERNAL_H
#define NOSV_INTERNAL_H

#include <stdatomic.h>

#include "nosv.h"
#include "generic/list.h"

struct nosv_worker;
typedef atomic_uint_fast64_t atomic_uint64_t;

struct nosv_task_type
{
	nosv_task_run_callback_t run_callback;
	nosv_task_end_callback_t end_callback;
	nosv_task_completed_callback_t completed_callback;
	void *metadata;
	const char *label;
	int pid;
};


struct nosv_task
{
	atomic_uint64_t event_count;
	size_t metadata;
	struct nosv_task_type *type;
	struct nosv_worker *worker;
	list_head_t list_hook;
	int priority;
};

#endif // NOSV_INTERNAL_H