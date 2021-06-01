/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPUS_H
#define CPUS_H

#include <sched.h>

#include "compiler.h"

typedef struct cpu {
	cpu_set_t cpuset;
	int system_id;
	int logic_id;
} cpu_t;

typedef struct cpumanager {
	int cpu_cnt;
	cpu_t cpus[]; // Flexible array
} cpumanager_t;

__internal void cpus_init(int initialize);
__internal int cpus_count();
__internal cpu_t *cpu_get(int cpu);

#endif // CPUS_H