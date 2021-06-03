/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_INTERNAL_H
#define NOSV_INTERNAL_H

#include "nosv.h"
#include "generic/list.h"

struct nosv_task_type
{
	int pid;
};


struct nosv_task
{
	struct nosv_task_type *type;
	list_head_t list_hook;
};

#endif // NOSV_INTERNAL_H