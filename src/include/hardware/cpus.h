/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPUS_H
#define CPUS_H

#include <sched.h>

#include "compiler.h"
#include "nosv.h"

typedef struct cpu {
	cpu_set_t cpuset;
	int system_id;
	int logic_id;
} cpu_t;

typedef struct cpumanager {
	int cpu_cnt; // Number of CPUs in the system
	int *pids_cpus; // Map from "Logical" PIDs to CPUs
	cpu_t cpus[]; // Flexible array
} cpumanager_t;

__internal void cpus_init(int initialize);
__internal int cpus_count();
__internal cpu_t *cpu_get(int cpu);
__internal cpu_t *cpu_pop_free();
__internal int cpu_get_pid(int cpu);
__internal void cpu_transfer(int destination_pid, cpu_t *cpu, nosv_task_t task);
__internal void cpu_mark_free(cpu_t *cpu);

__internal extern thread_local int __current_cpu;

static inline int cpu_get_current()
{
	return __current_cpu;
}

static inline void cpu_set_current(int cpu)
{
	__current_cpu = cpu;
}

#endif // CPUS_H