/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef TOPOLOGY_H
#define TOPOLOGY_H

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
	int s_max[NOSV_TOPO_LEVEL_COUNT];	// Max system id in this system for each level
} topology_t;

typedef struct cpumanager {
	int *pids_cpus; // Map from "Logical" PIDs to CPUs
	cpu_t cpus[];	// Flexible array
} cpumanager_t;

// Internal Topology API
void topology_init(int initialize);
void topology_free(void);
static inline int topology_get_parent_logical_id(nosv_topo_level_t son_level, int son_logical_id, nosv_topo_level_t parent);
static inline int topology_get_parent_system_id(nosv_topo_level_t son_level, int son_logical_id, nosv_topo_level_t parent);
static inline int topology_get_logical_id(nosv_topo_level_t level, int system_id);
static inline int topology_get_system_id(nosv_topo_level_t level, int logical_id);
static inline int topology_get_level_count(nosv_topo_level_t level);
static inline int topology_get_level_max(nosv_topo_level_t level);
static inline int *topology_get_system_id_arr(nosv_topo_level_t lvl);
static inline topo_domain_t *topology_get_domain(nosv_topo_level_t level, int logical_id);
static inline cpu_bitset_t *topology_get_cpu_logical_mask(nosv_topo_level_t level, int lid);
static inline cpu_bitset_t *topology_get_cpu_system_mask(nosv_topo_level_t level, int lid);
static inline cpu_bitset_t *topology_get_valid_domains_mask(nosv_topo_level_t level);
static inline const char *topology_get_level_name(nosv_topo_level_t level);
__internal int topology_get_default_affinity(char **out);

// Internal CPU API
__internal void cpu_affinity_reset(void);
__internal void cpu_transfer(int destination_pid, cpu_t *cpu, task_execution_handle_t handle);
__internal void cpu_mark_free(cpu_t *cpu);
__internal void cpu_get_all_mask(const char **mask);
__internal cpu_t *cpu_pop_free(int pid);
static inline int cpu_get_logical_id(cpu_t *cpu);
static inline int cpu_get_system_id(cpu_t *cpu);
static inline int cpu_get_parent_logical_id(cpu_t *cpu, nosv_topo_level_t level);
static inline cpu_t *cpu_get_from_logical_id(int cpu_logical_id);
static inline cpu_t *cpu_get_from_system_id(int cpu_system_id);
static inline void cpu_set_pid(cpu_t *cpu, int pid);

__internal extern thread_local int __current_cpu;
__internal extern cpumanager_t *cpumanager;
__internal extern topology_t *topology;

/* Static functions for the Topology API */
// Returns the logical id given the topology level and system id, -1 if not yet initialized
static inline int topology_get_logical_id(nosv_topo_level_t level, int system_id)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	if (topology->s_max[level] < 0)
		return TOPO_ID_UNSET; // Level not yet initialized

	if (system_id > topology->s_max[level])
		nosv_abort("system_id %d is larger than the maximum system_id %d for topology level %s", system_id, topology->s_max[level], nosv_topo_level_names[level]);

	assert(topology->s_to_l[level][system_id] >= -1);
	return topology->s_to_l[level][system_id];
}

// Returns the array of topo_domain_t structs for the given topology level.
static inline topo_domain_t *topology_get_level_domains(nosv_topo_level_t level)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	return topology->per_level_domains[level];
}

// Returns the system id given the topology level and logical id
static inline int topology_get_system_id(nosv_topo_level_t level, int logical_id)
{
	assert(logical_id >= 0 && topology->per_level_count[level]);
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	if (topology->s_max[level] < 0)
		return TOPO_ID_UNSET; // Level not yet initialized

	topo_domain_t *domains = topology_get_level_domains(level);
	int system_id = domains[logical_id].system_id;
	assert(system_id >= 0 && system_id <= topology->s_max[level]);
	return system_id;
}

// Returns the number of domains in the given topology level
static inline int topology_get_level_count(nosv_topo_level_t level)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	assert(topology->per_level_count[level] >= 1);

	return topology->per_level_count[level];
}

// Returns the name (char array) of the given topology level
static inline const char *topology_get_level_name(nosv_topo_level_t level)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	return nosv_topo_level_names[level];
}

// Returns max system id for the given topology level
static inline int topology_get_level_max(nosv_topo_level_t level)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	return topology->s_max[level];
}

// Returns the topo_domain_t struct for the given level and logical id.
static inline topo_domain_t *topology_get_domain(nosv_topo_level_t level, int logical_id)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	assert(logical_id >= 0);

	topo_domain_t *domains = topology_get_level_domains(level);
	return &domains[logical_id];
}

// Returns the logical id of the parent
static inline int topology_get_parent_logical_id(nosv_topo_level_t child_level, int child_logical_id, nosv_topo_level_t parent)
{
	// Node does not have parents
	assert(child_level >= NOSV_TOPO_LEVEL_NUMA && child_level <= NOSV_TOPO_LEVEL_CPU);
	assert(parent >= NOSV_TOPO_LEVEL_NODE && parent <= NOSV_TOPO_LEVEL_CPU);
	assert(child_logical_id >= 0 && child_logical_id < topology_get_level_count(child_level));
	assert(child_level >= parent);

	return topology->per_level_domains[child_level][child_logical_id].parents[parent];
}

// Returns the system id of the parent
static inline int topology_get_parent_system_id(nosv_topo_level_t child_level, int child_logical_id, nosv_topo_level_t parent)
{
	// Node does not have parents
	assert(child_level >= NOSV_TOPO_LEVEL_NUMA && child_level <= NOSV_TOPO_LEVEL_CPU);
	assert(parent >= NOSV_TOPO_LEVEL_NODE && parent <= NOSV_TOPO_LEVEL_CORE);
	assert(child_logical_id >= 0 && child_logical_id < topology_get_level_max(child_level));
	assert(child_level > parent);

	int logical_id = topology->per_level_domains[child_level][child_logical_id].parents[parent];

	return topology_get_system_id(child_level, logical_id);
}

// Returns the cpu_bitset_t of system cpus for the domain (given by level and logical id)
static inline cpu_bitset_t *topology_get_cpu_system_mask(nosv_topo_level_t level, int lid)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	topo_domain_t *domain = topology_get_domain(level, lid);
	return &(domain->cpu_sid_mask);
}

// Returns the cpu_bitset_t of logical cpus for the domain (given by level and logical id)
static inline cpu_bitset_t *topology_get_cpu_logical_mask(nosv_topo_level_t level, int lid)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	topo_domain_t *domain = topology_get_domain(level, lid);
	return &(domain->cpu_lid_mask);
}

// Returns a pointer to the cpu_bitset_t of valid domains for the given topology level
static inline cpu_bitset_t *topology_get_valid_domains_mask(nosv_topo_level_t level)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);

	return &(topology->per_level_valid_domains[level]);
}

// Allocates an array of size topology_get_level_count(lvl) and returns it filled with the available system ids of said level
static inline int *topology_get_system_id_arr(nosv_topo_level_t lvl)
{
	int *system_ids = malloc(topology_get_level_count(lvl) * sizeof(int));
	for (int i = 0; i < topology_get_level_count(lvl); i++) {
		system_ids[i] = topology_get_system_id(lvl, i);
	}
	return system_ids;
}

/* Static functions for the CPU API */
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

static inline int cpu_get_logical_id(cpu_t *cpu)
{
	return cpu->cpu_domain->logical_cpu;
}

static inline int cpu_get_system_id(cpu_t *cpu)
{
	return cpu->cpu_domain->system_id;
}

static inline void cpu_set_pid(cpu_t *cpu, int pid)
{
	assert(cpumanager->pids_cpus[cpu_get_logical_id(cpu)] < MAX_PIDS);
	cpumanager->pids_cpus[cpu_get_logical_id(cpu)] = pid;
}

// Returns the logical id of the parent in the topology level 'parent'
static inline int cpu_get_parent_logical_id(cpu_t *cpu, nosv_topo_level_t parent)
{
	assert(parent >= NOSV_TOPO_LEVEL_NODE && parent <= NOSV_TOPO_LEVEL_CPU);
	return topology_get_parent_logical_id(NOSV_TOPO_LEVEL_CPU, cpu_get_logical_id(cpu), parent);
}

static inline cpu_t *cpu_get_from_logical_id(int cpu_logical_id)
{
	return &cpumanager->cpus[cpu_logical_id];
}

static inline cpu_t *cpu_get_from_system_id(int cpu_system_id)
{
	int cpu_logical_id = topology_get_logical_id(NOSV_TOPO_LEVEL_CPU, cpu_system_id);
	if (cpu_logical_id < 0)
		return NULL;

	return &cpumanager->cpus[cpu_logical_id];
}

#endif // TOPOLOGY_H
