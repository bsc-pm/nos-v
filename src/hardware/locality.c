/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <numa.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "config/config.h"
#include "hardware/cpus.h"
#include "hardware/locality.h"
#include "nosv-internal.h"

__internal int *numa_logical_to_system;
__internal int *numa_system_to_logical;
__internal int numa_count;
__internal int numa_fromcfg;

static inline void locality_numa_disabled(void)
{
	numa_count = 1;
	numa_logical_to_system = malloc(sizeof(int));
	numa_system_to_logical = malloc(sizeof(int));

	numa_logical_to_system[0] = 0;
	numa_system_to_logical[0] = 0;

	numa_fromcfg = 0;
}

static inline void parse_from_libnuma(void)
{
	// Use numa_all_nodes_ptr as that contains only the nodes that are actually available,
	// not all configured. On some machines, some nodes are configured but unavailable.
	numa_count = numa_bitmask_weight(numa_all_nodes_ptr);
	int numa_max = numa_max_node() + 1;

	numa_logical_to_system = malloc(sizeof(int) * numa_count);
	numa_system_to_logical = malloc(sizeof(int) * numa_max);

	int logic_idx = 0;
	for (int i = 0; i < numa_max; ++i) {
		if (numa_bitmask_isbitset(numa_all_nodes_ptr, i)) {
			numa_system_to_logical[i] = logic_idx;
			numa_logical_to_system[logic_idx++] = i;
		} else {
			numa_system_to_logical[i] = -1;
		}
	}

	assert(logic_idx == numa_count);
	numa_fromcfg = 0;
}

static inline void parse_from_config(void)
{
	// Use the relevant configuration option
	int numa_max = nosv_config.affinity_numa_nodes.n;
	numa_logical_to_system = malloc(sizeof(int) * numa_max);
	numa_system_to_logical = malloc(sizeof(int) * numa_max);

	generic_array_t *numa_nodes = (generic_array_t *) nosv_config.affinity_numa_nodes.items;

	// In the logical index, we will skip empty NUMA nodes.
	int logic_idx = 0;
	for (int i = 0; i < numa_max; ++i) {
		if (numa_nodes[i].n > 0) {
			numa_system_to_logical[i] = logic_idx;
			numa_logical_to_system[logic_idx++] = i;
		} else {
			numa_system_to_logical[i] = -1;
		}
	}

	numa_count = logic_idx;
	numa_fromcfg = 1;
}

void locality_init(void)
{
	if (nosv_config.affinity_numa_nodes.n > 0) {
		parse_from_config();
	} else if (numa_available() != -1) {
		parse_from_libnuma();
	} else {
		locality_numa_disabled();
	}
}

int locality_numa_count(void)
{
	return numa_count;
}

static int locality_numa_from_cfg(int system_cpu_id)
{
	// Search for CPU's NUMA node.
	// If not found, return an error for the user to fix
	int num_nodes = nosv_config.affinity_numa_nodes.n;
	generic_array_t *numa_nodes = (generic_array_t *) nosv_config.affinity_numa_nodes.items;

	for (int i = 0; i < num_nodes; ++i) {
		int64_t *cpu_idx = (int64_t *)numa_nodes[i].items;
		int cpus = numa_nodes[i].n;

		for (int j = 0; j < cpus; ++j) {
			if (cpu_idx[j] == system_cpu_id)
				return i;
		}
	}

	nosv_abort("Reading NUMA nodes from config, but cpu %d is missing", system_cpu_id);
}

int locality_get_cpu_numa(int system_cpu_id)
{
	int cpu_numa_id = 0;
	if (numa_fromcfg)
		cpu_numa_id = locality_numa_from_cfg(system_cpu_id);
	else
		cpu_numa_id = numa_node_of_cpu(system_cpu_id);

	return numa_system_to_logical[cpu_numa_id];
}

int locality_get_logical_numa(int system_numa_id)
{
	return numa_system_to_logical[system_numa_id];
}

void locality_shutdown(void)
{
	free(numa_logical_to_system);
	free(numa_system_to_logical);
}

int locality_get_default_affinity(char **out)
{
	struct bitmask *all_affinity = numa_allocate_cpumask();
	int max_cpus = numa_num_possible_cpus();
	assert(all_affinity);

	numa_sched_getaffinity(0, all_affinity);

	if (numa_bitmask_weight(all_affinity) == 1) {
		// Affinity to a single core
		int i;
		for (i = 0; i < max_cpus - 1; ++i) {
			if (numa_bitmask_isbitset(all_affinity, i))
				break;
		}

		assert(numa_bitmask_isbitset(all_affinity, i));
		__maybe_unused int res = nosv_asprintf(out, "cpu-%d", i);
		assert(!res);
	} else {
		int selected_node = -1;
		for (int i = 0; i < max_cpus - 1; ++i) {
			if (numa_bitmask_isbitset(all_affinity, i)) {
				int node_of_cpu = numa_node_of_cpu(i);
				if (selected_node < 0)
					selected_node = node_of_cpu;

				if (selected_node != node_of_cpu) {
					// Cannot determine single node affinity
					numa_free_cpumask(all_affinity);
					return 1;
				}
			}
		}

		assert(selected_node >= 0);
		__maybe_unused int res = nosv_asprintf(out, "numa-%d", selected_node);
		assert(!res);

		// So far, we know all CPUs belong to a single node. Nevertheless, it is possible
		// that the node has more CPUs that we don't have an affinity to.
		// Detect this case and warn about it
		struct bitmask *node_affinity = numa_allocate_cpumask();
		numa_node_to_cpus(selected_node, node_affinity);

		if (!numa_bitmask_equal(all_affinity, node_affinity))
			nosv_warn("Affinity automatically set to numa-%d, but other non-affine CPUs are present in this node.", selected_node);

		numa_bitmask_free(node_affinity);
	}

	numa_free_cpumask(all_affinity);
	return 0;
}

/* Public API functions */
int nosv_get_num_numa_nodes(void)
{
	return numa_count;
}

int nosv_get_system_numa_id(int logical_numa_id)
{
	if (logical_numa_id >= numa_count)
		return NOSV_ERR_INVALID_PARAMETER;

	return numa_logical_to_system[logical_numa_id];
}

int nosv_get_logical_numa_id(int system_numa_id)
{
	// We don't store the max numa id, so this access could
	// be out-of-bounds
	return numa_system_to_logical[system_numa_id];
}

int nosv_get_num_cpus_in_numa(int system_numa_id)
{
	int logical_node = numa_system_to_logical[system_numa_id];
	int count = 0;
	for (int i = 0; i < cpus_count(); ++i)
		count += (cpu_get(i)->numa_node == logical_node);

	return count;
}
