/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_AFFINITY_H
#define NOSV_AFFINITY_H

#include "nosv.h"

#include <sched.h>
#include <stdint.h>

/* Fully generic affinity API */
/* Use cpu_set_t for everything */

struct nosv_affinity;
typedef nosv_affinity *nosv_affinity_t;

typedef enum nosv_affinity_type {
	PREFERRED,
	STRICT
} nosv_affinity_type_t;

/* Register a custom affinity mask */
int nosv_affinity_register(nosv_affinity_t *affinity /* out */, const cpu_set_t *cpus, const nosv_affinity_type_t type);
/* Make sure no tasks are using this affinity mask before unregistering it */
int nosv_affinity_unregister(nosv_affinity_t affinity);

/* For me it's unclear if we want to do per-task affinity or per task-type */
nosv_affinity_t nosv_get_task_affinity(nosv_task_t task);
void nosv_set_task_affinity(nosv_task_t task, nosv_affinity_t affinity);

static inline void example() {
	nosv_affinity_t affinity;
	nosv_task_t task;

	// Bind to CPU 1
	cpu_set_t cpus;
	CPU_SET(1, &cpus);
	nosv_affinity_register(&affinity, &cpus, STRICT);

	nosv_set_task_affinity(task, affinity);
}

#endif // NOSV_AFFINITY_H