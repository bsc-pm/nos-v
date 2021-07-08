/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef TASKS_H
#define TASKS_H

#include "compiler.h"
#include "nosv.h"
#include "generic/list.h"
#include "generic/spinlock.h"

typedef struct task_type_manager {
	nosv_spinlock_t lock;
	list_head_t types;
} task_type_manager_t;

__internal void task_execute(nosv_task_t task);

__internal void task_type_manager_init();
__internal list_head_t *task_type_manager_get_list();
__internal void task_type_manager_shutdown();

#endif // TASKS_H
