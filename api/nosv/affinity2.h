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
	SOCKET,
	NUMA,
	L3COMPLEX
} nosv_affinity_level_t;

typedef struct nosv_affinity {
	int index;
	nosv_affinity_type_t type;
	nosv_affinity_level_t level;
} nosv_affinity_t;

/* For me it's unclear if we want to do per-task affinity or per task-type */
void nosv_get_task_affinity(nosv_task_t task, nosv_affinity_t *affinity /* out */);
void nosv_set_task_affinity(nosv_task_t task, const nosv_affinity_t *affinity);

/* Example */

static inline void example() {
	nosv_affinity_t affinity;
	nosv_task_t task;

	// Bind to CPU 1
	affinity.level = CPU;
	affinity.type = STRICT;
	affinity.index = 1;
	nosv_set_task_affinity(task, &affinity);

	// Affinity to NUMA Node 0
	affinity.level = NUMA;
	affinity.type = PREFERRED;
	affinity.index = 0;
	nosv_set_task_affinity(task, &affinity);
}

#endif // NOSV_AFFINITY_H