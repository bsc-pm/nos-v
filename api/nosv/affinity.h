/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_AFFINITY_H
#define NOSV_AFFINITY_H

#include "nosv.h"

#include <sched.h>
#include <stdint.h>

typedef enum nosv_affinity_type {
	PREFERRED,
	STRICT
} nosv_affinity_type_t;

typedef enum nosv_affinity_level {
	CPU,
	NUMA,
	USER_COMPLEX
} nosv_affinity_level_t;

typedef uint32_t nosv_affinity_t;

static inline nosv_affinity_t nosv_affinity_get(uint32_t index, nosv_affinity_level_t level, nosv_affinity_type_t type)
{
	return (level << 30) | (type << 29) | index;
}

/* For me it's unclear if we want to do per-task affinity or per task-type */
nosv_affinity_t nosv_get_task_affinity(nosv_task_t task);
void nosv_set_task_affinity(nosv_task_t task, nosv_affinity_t affinity);

/* Example */
/*
static inline void example() {
	nosv_affinity_t affinity;
	nosv_task_t task;

	// Bind to CPU 1
	affinity = nosv_affinity_get(1, CPU, STRICT);
	nosv_set_task_affinity(task, affinity);

	// Affinity to NUMA Node 0
	affinity = nosv_affinity_get(0, NUMA, PREFERRED);
	nosv_set_task_affinity(task, affinity);
}
*/
#endif // NOSV_AFFINITY_H