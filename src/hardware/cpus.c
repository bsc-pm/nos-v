/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <errno.h>

#include "compiler.h"
#include "hardware/cpus.h"
#include "hardware/locality.h"
#include "hardware/threads.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"

thread_local int __current_cpu = -1;
__internal cpumanager_t *cpumanager;

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

	// Find out maximum CPU id
	int maxcpu = 0;
	int i = CPU_SETSIZE;
	while (i >= 0) {
		if (CPU_ISSET(i, &set)) {
			maxcpu = i;
			break;
		}
		--i;
	}
	cpumanager->system_to_logical = salloc(sizeof(int) * (maxcpu + 1), 0);

	i = 0;
	int curr = 0;
	while (i < cnt) {
		if (CPU_ISSET(curr, &set)) {
			CPU_ZERO(&cpumanager->cpus[i].cpuset);
			CPU_SET(curr, &cpumanager->cpus[i].cpuset);
			cpumanager->cpus[i].system_id = curr;
			cpumanager->cpus[i].logic_id = i;
			cpumanager->cpus[i].numa_node = locality_get_cpu_numa(curr);
			cpumanager->system_to_logical[curr] = i;
			++i;
			maxcpu = curr;
		} else {
			cpumanager->system_to_logical[curr] = -1;
		}

		++curr;
	}

	cpumanager->pids_cpus = salloc(sizeof(int) * cnt, 0);
	// Initialize the mapping as empty
	for (i = 0; i < cnt; ++i) {
		cpumanager->pids_cpus[i] = -1;
	}
}

int cpu_system_to_logical(int cpu)
{
	return cpumanager->system_to_logical[cpu];
}

int cpus_count()
{
	return cpumanager->cpu_cnt;
}

cpu_t *cpu_get(int cpu)
{
	return &cpumanager->cpus[cpu];
}

cpu_t *cpu_pop_free(int pid)
{
	for (int i = 0; i < cpus_count(); ++i) {
		if (cpumanager->pids_cpus[i] == -1) {
			cpumanager->pids_cpus[i] = pid;
			return cpu_get(i);
		}
	}

	return NULL;
}

void cpu_set_pid(cpu_t *cpu, int pid)
{
	assert(cpumanager->pids_cpus[cpu->logic_id] < MAX_PIDS);
	cpumanager->pids_cpus[cpu->logic_id] = pid;
}

void cpu_mark_free(cpu_t *cpu)
{
	cpumanager->pids_cpus[cpu->logic_id] = -1;
}

void cpu_transfer(int destination_pid, cpu_t *cpu, nosv_task_t task)
{
	assert(cpu);
	cpumanager->pids_cpus[cpu->logic_id] = destination_pid;

	// Wake up a worker from another PID to take over
	worker_wake_idle(destination_pid, cpu, task);
}

int nosv_get_num_cpus(void)
{
	return cpus_count();
}

int nosv_get_current_logical_cpu(void)
{
	if (!worker_is_in_task())
		return -EINVAL;

	return cpu_get_current();
}

int nosv_get_current_system_cpu(void)
{
	if (!worker_is_in_task())
		return -EINVAL;

	cpu_t *cpu = cpu_get(cpu_get_current());
	assert(cpu);

	return cpu->system_id;
}
