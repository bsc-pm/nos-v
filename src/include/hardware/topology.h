/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
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
	union {
		struct {
			int logical_numa;
			int logical_complex_set;
			int logical_core;
		};
		int parents[NOSV_TOPO_LEVEL_COUNT-2];
	};
	int system_id;
	cpu_bitset_t cpu_sid_mask; // system ids
	cpu_bitset_t cpu_lid_mask; // logic ids
} topo_domain_t;

typedef struct cpu {
	cpu_set_t cpuset;
	union {
		struct { // Logic ids
			int logical_numa;
			int logical_complex_set;
			int logical_core;
			int logical_id;
		};
		int parents[NOSV_TOPO_LEVEL_COUNT - 1]; // upper levels and itself
	};

	int system_id;
	cpu_hwcounters_t counters;
} cpu_t;

typedef struct topology {
	union {
		struct {
			cpu_bitset_t valid_numas;
			cpu_bitset_t valid_complex_sets;
			cpu_bitset_t valid_cores;
			cpu_bitset_t valid_cpus;
		};
		cpu_bitset_t per_domain_bitset[NOSV_TOPO_LEVEL_COUNT - 1];
	};

	union {
		struct {
			int node_count;
			int numa_count;
			int complex_set_count;
			int core_count;
			int cpu_count;
		};
		int per_domain_count[NOSV_TOPO_LEVEL_COUNT];
	};
	bool numa_fromcfg;

	union {
		struct {
			topo_domain_t *numas; // NUMA topo domain array
			topo_domain_t *complex_sets; // Complex Set domain array
			topo_domain_t *cores; // Core domain array
			// CPU array belongs in cpumanager
		};
		topo_domain_t *(per_domain_array[NOSV_TOPO_LEVEL_COUNT-2]); // Should be accessed through topology_get_level_domains
	};

	int *s_to_l[NOSV_TOPO_LEVEL_COUNT]; // System id to logical id mapping
	int s_max[NOSV_TOPO_LEVEL_COUNT]; // Max system id in this system for each level
} topology_t;

typedef struct cpumanager {
	int *pids_cpus;			// Map from "Logical" PIDs to CPUs
	cpu_t cpus[];           // Flexible array
} cpumanager_t;

__internal void topology_init(int initialize);
__internal cpu_t *cpu_get(int cpu);
__internal cpu_t *cpu_pop_free(int pid);
__internal void cpu_set_pid(cpu_t *cpu, int pid);
__internal void cpu_transfer(int destination_pid, cpu_t *cpu, task_execution_handle_t handle);
__internal void cpu_mark_free(cpu_t *cpu);
__internal void cpu_affinity_reset(void);
__internal void cpu_get_all_mask(const char **mask);
__internal int cpu_get_parent_logical_id(cpu_t *cpu, nosv_topo_level_t level);


__internal int topology_get_logical(nosv_topo_level_t d, int system_id);
__internal int topology_get_system(nosv_topo_level_t level, int logical_id);
__internal int topology_get_level_count(nosv_topo_level_t d);
__internal int topology_get_default_affinity(char **out);
__internal cpu_bitset_t* topology_get_domain_cpu_logical_mask(nosv_topo_level_t level, int lid);
__internal cpu_bitset_t* topology_get_domain_cpu_system_mask(nosv_topo_level_t level, int lid);

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
