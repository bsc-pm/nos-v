/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_AFFINITY_H
#define NOSV_AFFINITY_H

#include "nosv.h"

#include <sched.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nosv_affinity_type {
	NOSV_AFFINITY_TYPE_PREFERRED,
	NOSV_AFFINITY_TYPE_STRICT
} nosv_affinity_type_t;

typedef enum nosv_affinity_level {
	NOSV_AFFINITY_LEVEL_NONE,
	NOSV_AFFINITY_LEVEL_CPU,
	NOSV_AFFINITY_LEVEL_NUMA,
	NOSV_AFFINITY_LEVEL_USER_COMPLEX
} nosv_affinity_level_t;

typedef struct nosv_affinity {
	nosv_affinity_level_t level : 2;
	nosv_affinity_type_t type : 1;
	uint32_t index : 29;
} nosv_affinity_t;

static inline nosv_affinity_t nosv_affinity_get(uint32_t index, nosv_affinity_level_t level, nosv_affinity_type_t type)
{
	nosv_affinity_t affinity;
	affinity.index = index;
	affinity.level = level;
	affinity.type = type;
	return affinity;
}

nosv_affinity_t nosv_get_task_affinity(nosv_task_t task);
void nosv_set_task_affinity(nosv_task_t task, nosv_affinity_t *affinity);

#ifdef __cplusplus
}
#endif

#endif // NOSV_AFFINITY_H
