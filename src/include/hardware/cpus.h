/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPUS_H
#define CPUS_H

#include <assert.h>
#include <sched.h>

#include "compiler.h"
#include "nosv.h"
#include "hwcounters/cpuhwcounters.h"
#include "system/tasks.h"

typedef struct cpu {
	cpu_set_t cpuset;
	int system_id;
	int logic_id;
	int numa_node;
	cpu_hwcounters_t counters;
} cpu_t;

typedef struct cpumanager {
	cpu_set_t all_cpu_set;  // cpuset containing the original affinity mask
	int *pids_cpus;			// Map from "Logical" PIDs to CPUs
	int *system_to_logical; // Map from system CPU ids to logical cpu ids
	int cpu_cnt;            // Number of available CPUs in the system
	int num_siblings_list;  // Number of distinct thread siblings lists
	int **thread_siblings;  // Lists of thread siblings indexed by CPU ids (at most num_siblings_list)
	cpu_t cpus[];           // Flexible array
} cpumanager_t;

__internal void cpus_init(int initialize);
__internal int cpus_count(void);
__internal cpu_t *cpu_get(int cpu);
__internal cpu_t *cpu_pop_free(int pid);
__internal void cpu_set_pid(cpu_t *cpu, int pid);
__internal void cpu_transfer(int destination_pid, cpu_t *cpu, task_execution_handle_t handle);
__internal void cpu_mark_free(cpu_t *cpu);
__internal int cpu_system_to_logical(int cpu);
__internal void cpu_affinity_reset(void);
__internal void cpu_get_all_mask(const char **mask);

__internal extern thread_local int __current_cpu;
__internal extern cpumanager_t *cpumanager;

static inline int cpu_get_current(void)
{
	return __current_cpu;
}

static inline void cpu_set_current(int cpu)
{
	__current_cpu = cpu;
}

static inline int cpu_get_pid(int cpu)
{
	assert(cpumanager->pids_cpus[cpu] < MAX_PIDS);
	return cpumanager->pids_cpus[cpu];
}

#endif // CPUS_H
