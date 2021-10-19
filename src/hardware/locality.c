/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <numa.h>
#include <stdlib.h>

#include "compiler.h"
#include "hardware/locality.h"

__internal int *numa_logical_to_system;
__internal int *numa_system_to_logical;
__internal int numa_count;

static inline void locality_numa_disabled(void)
{
	numa_count = 1;
	numa_logical_to_system = malloc(sizeof(int));
	numa_system_to_logical = malloc(sizeof(int));

	numa_logical_to_system[0] = 0;
	numa_system_to_logical[0] = 0;
}

void locality_init(void)
{
	if (numa_available() == -1) {
		locality_numa_disabled();
		return;
	}

	numa_count = numa_num_configured_nodes();
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
}

int locality_numa_count(void)
{
	return numa_count;
}

int locality_get_cpu_numa(int system_cpu_id)
{
	return numa_system_to_logical[numa_node_of_cpu(system_cpu_id)];
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
