/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>

#include "hardware/cpus.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"

static cpumanager_t *cpumanager;

void cpus_init(int initialize)
{
	if (!initialize) {
		cpumanager = (cpumanager_t *)st_config.config->cpumanager_ptr;
		assert(cpumanager);
		return;
	}

	cpu_set_t set;
	sched_getaffinity(0, sizeof(set), &set);
	int cnt = CPU_COUNT(&set);

	// The CPU array is located just after the CPU manager, as a flexible array member.
	cpumanager = salloc(sizeof(cpumanager_t) + cnt * sizeof(cpu_t), 0);
	st_config.config->cpumanager_ptr = cpumanager;
	cpumanager->cpu_cnt = cnt;

	int i = 0;
	int curr = 0;
	while (i < cnt) {
		if (CPU_ISSET(curr, &set)) {
			CPU_ZERO(&cpumanager->cpus[i].cpuset);
			CPU_SET(curr, &cpumanager->cpus[i].cpuset);
			cpumanager->cpus[i].system_id = curr;
			cpumanager->cpus[i].logic_id = i;
			++i;
		}

		++curr;
	}
}

int cpus_count()
{
	return cpumanager->cpu_cnt;
}

cpu_t *cpu_get(int cpu)
{
	return &cpumanager->cpus[cpu];
}