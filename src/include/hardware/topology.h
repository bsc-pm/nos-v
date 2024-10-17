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
#include "hwcounters/cpuhwcounters.h"
#include "nosv.h"
#include "nosv/hwinfo.h"
#include "scheduler/cpubitset.h"
#include "system/tasks.h"


enum {
	TOPO_ID_DISABLED = -1,
	TOPO_ID_UNSET = -2
};

#define TOPO_NODE        (NOSV_TOPO_LEVEL_NODE)
#define TOPO_NUMA        (NOSV_TOPO_LEVEL_NUMA)
#define TOPO_COMPLEX_SET (NOSV_TOPO_LEVEL_COMPLEX_SET)
#define TOPO_CORE        (NOSV_TOPO_LEVEL_CORE)
#define TOPO_CPU         (NOSV_TOPO_LEVEL_CPU)
#define TOPO_LVL_COUNT   (NOSV_TOPO_LEVEL_COUNT)

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
		int parents[TOPO_LVL_COUNT];
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
	cpu_bitset_t per_level_valid_domains[TOPO_LVL_COUNT];
	bool numa_fromcfg;

	int per_level_count[TOPO_LVL_COUNT];

	topo_domain_t *(per_level_domains[TOPO_LVL_COUNT]); // Should be accessed through topology_get_level_domains

	int *s_to_l[TOPO_LVL_COUNT]; // System id to logical id mapping
	int s_max[TOPO_LVL_COUNT];	// Max system id in this system for each level
} topology_t;

typedef struct cpumanager {
	int *pids_cpus; // Map from "Logical" PIDs to CPUs
	cpu_t cpus[];	// Flexible array
} cpumanager_t;

// Internal Topology API
__internal void topo_init(int initialize);
__internal void topo_free(void);
__internal int topo_get_default_aff(char **out);

static inline int topo_dom_parent_lid(nosv_topo_level_t son_level, int son_logical_id, nosv_topo_level_t parent);
static inline int topo_dom_parend_sid(nosv_topo_level_t son_level, int son_logical_id, nosv_topo_level_t parent);
static inline int topo_dom_lid(nosv_topo_level_t level, int system_id);
static inline int topo_dom_sid(nosv_topo_level_t level, int logical_id);
static inline topo_domain_t *topo_dom_ptr(nosv_topo_level_t level, int logical_id);
static inline cpu_bitset_t *topo_dom_cpu_lid_bitset(nosv_topo_level_t level, int lid);
static inline cpu_bitset_t *topo_dom_cpu_sid_bitset(nosv_topo_level_t level, int lid);

static inline int topo_lvl_cnt(nosv_topo_level_t level);
static inline int topo_lvl_max(nosv_topo_level_t level);
static inline int *topo_lvl_sid_arr(nosv_topo_level_t lvl);
static inline topo_domain_t *topo_lvl_doms(nosv_topo_level_t level);
static inline cpu_bitset_t *topo_lvl_sid_bitset(nosv_topo_level_t level);
static inline const char *topo_lvl_name(nosv_topo_level_t level);

// Internal CPU API
__internal void cpu_affinity_reset(void);
__internal void cpu_transfer(int destination_pid, cpu_t *cpu, task_execution_handle_t handle);
__internal void cpu_mark_free(cpu_t *cpu);
__internal void cpu_get_all_mask(const char **mask);
__internal cpu_t *cpu_pop_free(int pid);
static inline int cpu_lid(cpu_t *cpu);
static inline int cpu_sid(cpu_t *cpu);
static inline int cpu_parent_lid(cpu_t *cpu, nosv_topo_level_t level);
static inline cpu_t *cpu_ptr(int cpu_logical_id);
static inline void cpu_set_pid(cpu_t *cpu, int pid);

__internal extern thread_local int __current_cpu;
__internal extern cpumanager_t *cpumanager;
__internal extern topology_t *topology;

// Static functions for the Topology API

// Returns the logical id given the topology level and system id, -1 if not yet initialized
static inline int topo_dom_lid(nosv_topo_level_t level, int system_id)
{
	assert(level >= TOPO_NODE && level <= TOPO_CPU);
	if (topology->s_max[level] < 0)
		return TOPO_ID_UNSET; // Level not yet initialized

	assert(system_id <= topology->s_max[level]);
	assert(topology->s_to_l[level][system_id] >= -1);

	return topology->s_to_l[level][system_id];
}

// Returns the array of topo_domain_t structs for the given topology level.
static inline topo_domain_t *topo_lvl_doms(nosv_topo_level_t level)
{
	assert(level >= TOPO_NODE && level <= TOPO_CPU);
	return topology->per_level_domains[level];
}

// Returns the system id given the topology level and logical id
static inline int topo_dom_sid(nosv_topo_level_t level, int logical_id)
{
	assert(logical_id >= 0 && topology->per_level_count[level]);
	assert(level >= TOPO_NODE && level <= TOPO_CPU);
	if (topology->s_max[level] < 0)
		return TOPO_ID_UNSET; // Level not yet initialized

	topo_domain_t *domains = topo_lvl_doms(level);
	int system_id = domains[logical_id].system_id;
	assert(system_id >= 0 && system_id <= topology->s_max[level]);
	return system_id;
}

// Returns the number of domains in the given topology level
static inline int topo_lvl_cnt(nosv_topo_level_t level)
{
	assert(level >= TOPO_NODE && level <= TOPO_CPU);
	assert(topology->per_level_count[level] >= 1);

	return topology->per_level_count[level];
}

// Returns the name (char array) of the given topology level
static inline const char *topo_lvl_name(nosv_topo_level_t level)
{
	assert(level >= TOPO_NODE && level <= TOPO_CPU);
	return nosv_topo_level_names[level];
}

// Returns max system id for the given topology level
static inline int topo_lvl_max(nosv_topo_level_t level)
{
	assert(level >= TOPO_NODE && level <= TOPO_CPU);
	return topology->s_max[level];
}

// Returns the topo_domain_t struct for the given level and logical id.
static inline topo_domain_t *topo_dom_ptr(nosv_topo_level_t level, int logical_id)
{
	assert(level >= TOPO_NODE && level <= TOPO_CPU);
	assert(logical_id >= 0);

	topo_domain_t *domains = topo_lvl_doms(level);
	return &domains[logical_id];
}

// Returns the logical id of the parent
static inline int topo_dom_parent_lid(nosv_topo_level_t child_level, int child_logical_id, nosv_topo_level_t parent)
{
	// Node does not have parents
	assert(child_level >= TOPO_NUMA && child_level <= TOPO_CPU);
	assert(parent >= TOPO_NODE && parent <= TOPO_CPU);
	assert(child_logical_id >= 0 && child_logical_id < topo_lvl_cnt(child_level));
	assert(child_level >= parent);

	return topology->per_level_domains[child_level][child_logical_id].parents[parent];
}

// Returns the system id of the parent
static inline int topo_dom_parend_sid(nosv_topo_level_t child_level, int child_logical_id, nosv_topo_level_t parent)
{
	// Node does not have parents
	assert(child_level >= TOPO_NUMA && child_level <= TOPO_CPU);
	assert(parent >= TOPO_NODE && parent <= TOPO_CORE);
	assert(child_logical_id >= 0 && child_logical_id < topo_lvl_max(child_level));
	assert(child_level > parent);

	int logical_id = topology->per_level_domains[child_level][child_logical_id].parents[parent];

	return topo_dom_sid(child_level, logical_id);
}

// Returns the cpu_bitset_t of system cpus for the domain (given by level and logical id)
static inline cpu_bitset_t *topo_dom_cpu_sid_bitset(nosv_topo_level_t level, int lid)
{
	assert(level >= TOPO_NODE && level <= TOPO_CPU);
	topo_domain_t *domain = topo_dom_ptr(level, lid);
	return &(domain->cpu_sid_mask);
}

// Returns the cpu_bitset_t of logical cpus for the domain (given by level and logical id)
static inline cpu_bitset_t *topo_dom_cpu_lid_bitset(nosv_topo_level_t level, int lid)
{
	assert(level >= TOPO_NODE && level <= TOPO_CPU);
	topo_domain_t *domain = topo_dom_ptr(level, lid);
	return &(domain->cpu_lid_mask);
}

// Returns a pointer to the cpu_bitset_t of valid domains for the given topology level
static inline cpu_bitset_t *topo_lvl_sid_bitset(nosv_topo_level_t level)
{
	assert(level >= TOPO_NODE && level <= TOPO_CPU);

	return &(topology->per_level_valid_domains[level]);
}

// Allocates an array of size topology_get_level_count(lvl) and returns it filled with the available system ids of said level
static inline int *topo_lvl_sid_arr(nosv_topo_level_t lvl)
{
	int *system_ids = malloc(topo_lvl_cnt(lvl) * sizeof(int));

	for (int i = 0; i < topo_lvl_cnt(lvl); i++)
		system_ids[i] = topo_dom_sid(lvl, i);

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

static inline int cpu_lid(cpu_t *cpu)
{
	return cpu->cpu_domain->logical_cpu;
}

static inline int cpu_sid(cpu_t *cpu)
{
	return cpu->cpu_domain->system_id;
}

static inline void cpu_set_pid(cpu_t *cpu, int pid)
{
	assert(cpumanager->pids_cpus[cpu_lid(cpu)] < MAX_PIDS);
	cpumanager->pids_cpus[cpu_lid(cpu)] = pid;
}

// Returns the logical id of the parent in the topology level 'parent'
static inline int cpu_parent_lid(cpu_t *cpu, nosv_topo_level_t parent)
{
	assert(parent >= TOPO_NODE && parent <= TOPO_CPU);
	return topo_dom_parent_lid(TOPO_CPU, cpu_lid(cpu), parent);
}

// Returns pointer to the cpu_ptr for the logical id passed as argument
static inline cpu_t *cpu_ptr(int cpu_logical_id)
{
	return &cpumanager->cpus[cpu_logical_id];
}

#endif // TOPOLOGY_H
