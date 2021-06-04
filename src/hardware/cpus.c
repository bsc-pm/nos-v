/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>

#include "compiler.h"
#include "hardware/cpus.h"
#include "hardware/threads.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"

thread_local int __current_cpu = -1;

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

	cpumanager->pids_cpus = salloc(sizeof(int) * cnt, 0);
	// Initialize the mapping as empty
	for (i = 0; i < cnt; ++i) {
		cpumanager->pids_cpus[i] = -1;
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

int cpu_get_pid(int cpu)
{
	assert(cpumanager->pids_cpus[cpu] < MAX_PIDS);
	return cpumanager->pids_cpus[cpu];
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
	worker_wake(destination_pid, cpu, task);
	// And sleep on this one
	worker_idle();
}