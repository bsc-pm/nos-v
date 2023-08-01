/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "compiler.h"
#include "config/config.h"
#include "hardware/cpus.h"
#include "hardware/locality.h"
#include "hardware/threads.h"
#include "hwcounters/cpuhwcounters.h"
#include "instr.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"
#include "monitoring/monitoring.h"
#include "support/affinity.h"

#define SYS_CPU_PATH "/sys/devices/system/cpu"

thread_local int __current_cpu = -1;
__internal cpumanager_t *cpumanager;

static inline void cpu_find_siblings(cpu_set_t *set)
{
	int num_cpus = CPU_COUNT(set);
	assert(num_cpus > 0);

	// Keeps track of which CPUs have been visited
	cpu_set_t cpu_id_visited;
	CPU_ZERO(&cpu_id_visited);

	// Allocate space for each of the lists
	cpumanager->thread_siblings = salloc(sizeof(int *) * num_cpus, 0);
	cpumanager->num_siblings_list = 0;
	for (int i = 0; i < num_cpus; ++i) {
		cpumanager->thread_siblings[i] = salloc(sizeof(int) * num_cpus, 0);
		for (int j = 0; j < num_cpus; ++j) {
			cpumanager->thread_siblings[i][j] = -1;
		}
	}

	int curr_cpu_id = 0;
	int curr_list_id = 0;
	int num_assigned_cpus = 0;
	while (num_assigned_cpus < num_cpus) {
		// Make sure this CPU is in the process' mask
		if (CPU_ISSET(curr_cpu_id, set)) {
			// Only enter if this CPU has not been marked as visited
			if (!CPU_ISSET(curr_cpu_id, &cpu_id_visited)) {
				CPU_SET(curr_cpu_id, &cpu_id_visited);
				int sibling_id = 0;

				// Insert this CPU id as the first id of the list
				cpumanager->num_siblings_list++;
				cpumanager->thread_siblings[curr_list_id][sibling_id++] = curr_cpu_id;

				char cpu_label[100];
				snprintf(cpu_label, 100, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", curr_cpu_id);
				FILE *fp = fopen(cpu_label, "r");
				if (!fp) {
					nosv_abort("Couldn't parse CPU thread siblings list");
				}

				// Read the whole list of thread siblings (separated by "," and "-")
				char thread_siblings[400];
				fgets(thread_siblings, 400, fp);

				// First traverse the list by commas
				char *comma_token = strtok(thread_siblings, ",");
				while (comma_token) {
					// Once we have one of the tokens, find out if it's a dash-separated list
					int first_id, last_id;
					int ret = sscanf(comma_token, "%d-%d", &first_id, &last_id);
					if (ret == 1) {
						// Only one variable was filled, this is a single CPU
						if (first_id != curr_cpu_id && CPU_ISSET(first_id, set)) {
							CPU_SET(first_id, &cpu_id_visited);
							cpumanager->thread_siblings[curr_list_id][sibling_id++] = first_id;
						}
					} else if (ret == 2) {
						// Two variables were filled, this is a dash-separated CPU list
						for (int j = first_id; j <= last_id; ++j) {
							if (j != curr_cpu_id && CPU_ISSET(j, set)) {
								CPU_SET(j, &cpu_id_visited);
								cpumanager->thread_siblings[curr_list_id][sibling_id++] = j;
							}
						}
					} else {
						// No variable was filled, there was an error
						nosv_abort("Couldn't parse part of a CPU thread siblings list");
					}

					comma_token = strtok(NULL, ",");
				}

				fclose(fp);
				++curr_list_id;
			}
			++num_assigned_cpus;
		}
		++curr_cpu_id;
	}
}

// Parse a CPU set which is specified in separation by "-" and "," into a CPU set
static inline void cpu_parse_set(cpu_set_t *set, char *string_to_parse)
{
	CPU_ZERO(set);

	char *tok = strtok(string_to_parse, ",");
	while(tok) {
		int first_id, last_id;
		int ret = sscanf(tok, "%d-%d", &first_id, &last_id);
		if (ret == 1)
			last_id = first_id;
		else if (ret == 0)
			nosv_abort("Could not parse cpu list");

		for (int i = first_id; i <= last_id; ++i)
			CPU_SET(i, set);

		tok = strtok(NULL, ",");
	}
}

// Get a valid binding mask which includes all online CPUs in the system
void cpu_get_all_mask(const char **mask)
{
	// One would reasonably think that we can discover all runnable CPUs by
	// parsing /sys/devices/system/cpu/online, but the A64FX reports as online
	// CPUs 0-1,12-59, and 0-1 are not actually usable CPUs.
	// The only way to properly determine CPUs there is to use the parsed mask
	// for sched_setaffinity and then get the "corrected" affinity back with
	// sched_setaffinity.

	cpu_set_t set, bkp;
	char online_mask[400];

	FILE *fp = fopen(SYS_CPU_PATH "/online", "r");
	if (!fp)
		nosv_abort("Failed to open online CPU list");

	fgets(online_mask, sizeof(online_mask), fp);
	fclose(fp);

	cpu_parse_set(&set, online_mask);

	// Now the A64FX dance
	bypass_sched_getaffinity(0, sizeof(cpu_set_t), &bkp);
	bypass_sched_setaffinity(0, sizeof(cpu_set_t), &set);
	bypass_sched_getaffinity(0, sizeof(cpu_set_t), &set);
	bypass_sched_setaffinity(0, sizeof(cpu_set_t), &bkp);

	// At this point, "set" should contain all *really* online CPUs
	// Now we have to translate it to an actual mask
	assert(CPU_COUNT(&set) > 0);
	int maxcpu = CPU_SETSIZE - 1;
	while (!CPU_ISSET(maxcpu, &set))
		--maxcpu;

	assert(maxcpu >= 0);
	int num_digits = round_up_div(maxcpu, 4);
	int str_size = num_digits + 2 /* 0x */ + 1 /* \0 */;

	char *res = malloc(str_size);
	res[str_size - 1] = '\0';
	res[0] = '0';
	res[1] = 'x';
	char *curr_digit = &res[str_size - 2];

	for (int i = 0; i < num_digits; ++i) {
		int tmp = 0;
		for (int cpu = i * 4; cpu < (i + 1) * 4 && cpu <= maxcpu; ++cpu)
			tmp |= ((!!CPU_ISSET(cpu, &set)) << (cpu % 4));

		if (tmp < 10)
			*curr_digit = '0' + tmp;
		else
			*curr_digit = 'a' - 10 + tmp;

		curr_digit--;
	}

	*mask = res;
}

static inline void cpus_get_binding_mask(const char *binding, cpu_set_t *cpuset)
{
	assert(binding);
	assert(cpuset);

	CPU_ZERO(cpuset);

	if (strcmp(binding, "inherit") == 0) {
		bypass_sched_getaffinity(0, sizeof(cpu_set_t), cpuset);
		assert(CPU_COUNT(cpuset) > 0);
	} else {
		if (binding[0] != '0' || (binding[1] != 'x' && binding[1] != 'X')) {
			nosv_abort("invalid binding mask");
		}

		const int len = strlen(binding);
		for (int c = len-1, b = 0; c >= 2; --c, b += 4) {
			int number = 0;
			if (binding[c] >= '0' && binding[c] <= '9') {
				number = binding[c] - '0';
			} else if (binding[c] >= 'a' && binding[c] <= 'f') {
				number = (binding[c] - 'a') + 10;
			} else if (binding[c] >= 'A' && binding[c] <= 'F') {
				number = (binding[c] - 'A') + 10;
			} else {
				nosv_abort("Invalid binding mask");
			}
			assert(number >= 0 && number < 16);

			if (number & 0x1) CPU_SET(b + 0, cpuset);
			if (number & 0x2) CPU_SET(b + 1, cpuset);
			if (number & 0x4) CPU_SET(b + 2, cpuset);
			if (number & 0x8) CPU_SET(b + 3, cpuset);
		}
	}
}

void cpus_init(int initialize)
{
	if (!initialize) {
		cpumanager = (cpumanager_t *)st_config.config->cpumanager_ptr;
		assert(cpumanager);
		return;
	}

	cpu_set_t set;
	cpus_get_binding_mask(nosv_config.cpumanager_binding, &set);

	int cnt = CPU_COUNT(&set);
	assert(cnt > 0);

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


	// Inform the instrumentation of all available CPUs
	instr_cpu_count(cnt, maxcpu);

	assert(cnt <= CPU_SETSIZE);

	// Construct lists of thread siblings
	cpu_find_siblings(&set);

	// We will order CPUs depending on their physical package. In this way, we
	// will intertwine CPUs to leverage all resources instead of just resource-
	// sharing threads
	int ordered_cpus[CPU_SETSIZE];
	int ordered_id = 0;
	int current_loop = 0;
	while (ordered_id < cnt) {
		for (i = 0; i < cpumanager->num_siblings_list; ++i) {
			if (cpumanager->thread_siblings[i][current_loop] != -1) {
				ordered_cpus[ordered_id++] = cpumanager->thread_siblings[i][current_loop];
			}
		}
		current_loop++;
	}
	assert(ordered_id == cnt);

	i = 0;
	int j = 0;
	int curr = ordered_cpus[j];
	while (i < cnt) {
		if (CPU_ISSET(curr, &set)) {
			CPU_ZERO(&cpumanager->cpus[i].cpuset);
			CPU_SET(curr, &cpumanager->cpus[i].cpuset);
			cpumanager->cpus[i].system_id = curr;
			cpumanager->cpus[i].logic_id = i;
			cpumanager->cpus[i].numa_node = locality_get_cpu_numa(curr);
			cpumanager->system_to_logical[curr] = i;
			cpuhwcounters_initialize(&(cpumanager->cpus[i].counters));

			// Inform the instrumentation of a new CPU
			instr_cpu_id(i, curr);

			++i;
		} else {
			cpumanager->system_to_logical[curr] = -1;
		}

		curr = ordered_cpus[++j];
	}

	cpumanager->pids_cpus = salloc(sizeof(int) * cnt, 0);
	// Initialize the mapping as empty
	for (i = 0; i < cnt; ++i) {
		cpumanager->pids_cpus[i] = -1;
	}

	cpumanager->all_cpu_set = set;
}

int cpu_system_to_logical(int cpu)
{
	return cpumanager->system_to_logical[cpu];
}

int cpus_count(void)
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

			// A CPU is going to become active
			monitoring_cpu_active(i);

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

	// A CPU just went idle
	monitoring_cpu_idle(cpu->logic_id);
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
		return NOSV_ERR_OUTSIDE_TASK;

	return cpu_get_current();
}

int nosv_get_current_system_cpu(void)
{
	if (!worker_is_in_task())
		return NOSV_ERR_OUTSIDE_TASK;

	cpu_t *cpu = cpu_get(cpu_get_current());
	assert(cpu);

	return cpu->system_id;
}

void cpu_affinity_reset(void)
{
	instr_affinity_set(-1);
	bypass_sched_setaffinity(0, sizeof(cpumanager->all_cpu_set), &cpumanager->all_cpu_set);
}
