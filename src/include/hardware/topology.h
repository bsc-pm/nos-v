/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPUS_H
#define CPUS_H

#include <assert.h>
#include <stdbool.h>
#include <sched.h>

#include "compiler.h"
#include "nosv.h"
#include "hwcounters/cpuhwcounters.h"
#include "scheduler/cpubitset.h"
#include "system/tasks.h"


enum {
	TOPO_ID_DISABLED = -1,
	TOPO_ID_UNSET = -2
};

typedef enum nosv_topo_level {
	NOSV_TOPO_LEVEL_NODE = 0,
	NOSV_TOPO_LEVEL_NUMA,
	NOSV_TOPO_LEVEL_COMPLEX_SET,
	NOSV_TOPO_LEVEL_CORE,
	NOSV_TOPO_LEVEL_CPU,
	NOSV_TOPO_LEVEL_COUNT
} nosv_topo_level_t;

static const char *const nosv_topo_level_names[] = {
	"node",
	"numa",
	"complex_set",
	"core",
	"cpu",
};

// Struct to be used for cores, complex sets and numas. For numa, we are wasting sizeof(int)*2 bytes, but it's not a big deal
typedef struct topo_domain {
	nosv_topo_level_t level;
	union {
		struct {
			int logical_node;
			int logical_numa;
			int logical_complex_set;
			int logical_core;
			int logical_cpu;
		};
		int parents[NOSV_TOPO_LEVEL_COUNT];
	};
	int system_id;
	cpu_bitset_t cpu_sid_mask; // system ids
	cpu_bitset_t cpu_lid_mask; // logic ids
} topo_domain_t;

typedef struct cpu {
	cpu_set_t cpuset;

	// Pointer to a position in topology's cpu array
	topo_domain_t *cpu_domain;

	int system_id;
	cpu_hwcounters_t counters;
} cpu_t;

typedef struct topology {
	cpu_bitset_t per_level_valid_domains[NOSV_TOPO_LEVEL_COUNT];
	bool numa_fromcfg;

	int per_level_count[NOSV_TOPO_LEVEL_COUNT];

	topo_domain_t *(per_level_domains[NOSV_TOPO_LEVEL_COUNT]); // Should be accessed through topology_get_level_domains

	int *s_to_l[NOSV_TOPO_LEVEL_COUNT]; // System id to logical id mapping
	int s_max[NOSV_TOPO_LEVEL_COUNT]; // Max system id in this system for each level
} topology_t;

typedef struct cpumanager {
	int *pids_cpus;			// Map from "Logical" PIDs to CPUs
	cpu_t cpus[];           // Flexible array
} cpumanager_t;

__internal void topology_init(int initialize);
__internal cpu_t *cpu_get_from_logical_id(int cpu_logical_id);
__internal cpu_t *cpu_get_from_system_id(int cpu_system_id);
__internal cpu_t *cpu_pop_free(int pid);
__internal void cpu_set_pid(cpu_t *cpu, int pid);
__internal void cpu_transfer(int destination_pid, cpu_t *cpu, task_execution_handle_t handle);
__internal void cpu_mark_free(cpu_t *cpu);
__internal void cpu_affinity_reset(void);
__internal void cpu_get_all_mask(const char **mask);
__internal int cpu_get_parent_logical_id(cpu_t *cpu, nosv_topo_level_t level);
__internal int cpu_get_logical_id(cpu_t *cpu);
__internal int cpu_get_system_id(cpu_t *cpu);
__internal int cpu_get_parent_logical_id(cpu_t *cpu, nosv_topo_level_t level);


__internal int topology_get_logical_id(nosv_topo_level_t level, int system_id);
__internal int topology_get_system_id(nosv_topo_level_t level, int logical_id);
__internal int topology_get_level_count(nosv_topo_level_t level);
__internal int topology_get_level_max(nosv_topo_level_t level);
__internal int topology_get_default_affinity(char **out);
__internal int topology_get_parent_logical_id(topo_domain_t *domain, nosv_topo_level_t parent);
__internal cpu_bitset_t* topology_get_cpu_logical_mask(nosv_topo_level_t level, int lid);
__internal cpu_bitset_t* topology_get_cpu_system_mask(nosv_topo_level_t level, int lid);
__internal cpu_bitset_t *topology_get_valid_domains_mask(nosv_topo_level_t level);
__internal const char *topology_get_level_name(nosv_topo_level_t level);

__internal extern thread_local int __current_cpu;
__internal extern cpumanager_t *cpumanager;
__internal extern topology_t *config_numa_count;

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
