/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include <stdbool.h>
#include <stdio.h>

#include "hardware/cpus.h"
#include "monitoring/cpumonitor.h"


void cpumonitor_initialize(cpumonitor_t *monitor)
{
	assert(monitor != NULL);

	monitor->num_cpus = (size_t) cpus_count();
	monitor->cpu_stats = (cpu_stats_t *) salloc(sizeof(cpu_stats_t) * monitor->num_cpus, -1);
	assert(monitor->cpu_stats != NULL);

	// Initialize the fields of each CPU statistics
	for (size_t id = 0; id < monitor->num_cpus; ++id) {
		cpu_stats_init(&(monitor->cpu_stats[id]));
	}
}

void cpumonitor_free(cpumonitor_t *monitor)
{
	assert(monitor != NULL);

	sfree(monitor->cpu_stats, sizeof(cpu_stats_t) * monitor->num_cpus, -1);
}

void cpumonitor_cpu_active(cpumonitor_t *monitor, int cpu_id)
{
	assert(monitor != NULL);
	assert(monitor->cpu_stats != NULL);
	assert((size_t) cpu_id < monitor->num_cpus);

	cpu_stats_active(&(monitor->cpu_stats[cpu_id]));
}

void cpumonitor_cpu_idle(cpumonitor_t *monitor, int cpu_id)
{
	assert(monitor != NULL);
	assert(monitor->cpu_stats != NULL);
	assert((size_t) cpu_id < monitor->num_cpus);

	cpu_stats_idle(&(monitor->cpu_stats[cpu_id]));
}

double cpumonitor_get_activeness(cpumonitor_t *monitor, int cpu_id)
{
	// NOTE: Only use at runtime shutdown, since this is not thread-safe yet
	assert(monitor != NULL);
	assert(monitor->cpu_stats != NULL);
	assert((size_t) cpu_id < monitor->num_cpus);

	return cpu_stats_get_activeness(&(monitor->cpu_stats[cpu_id]));
}

double cpumonitor_get_total_activeness(cpumonitor_t *monitor)
{
	// NOTE: Only use at runtime shutdown, since this is not thread-safe yet
	assert(monitor != NULL);
	assert(monitor->cpu_stats != NULL);

	double total_activeness = 0.0;
	for (size_t id = 0; id < monitor->num_cpus; ++id) {
		total_activeness += cpu_stats_get_activeness(&(monitor->cpu_stats[id]));
	}

	return total_activeness;
}

size_t cpumonitor_get_num_cpus(cpumonitor_t *monitor)
{
	assert(monitor != NULL);

	return monitor->num_cpus;
}

void cpumonitor_statistics(cpumonitor_t *monitor)
{
	assert(monitor != NULL);
	assert(monitor->cpu_stats != NULL);

	printf("+-----------------------------+\n");
	printf("|       CPU STATISTICS        |\n");
	printf("+-----------------------------+\n");
	printf("|   CPU(id) - Activeness(%%)   |\n");
	printf("+-----------------------------+\n");

	// Iterate through all CPUs and print their ID and activeness
	for (size_t id = 0; id < monitor->num_cpus; ++id) {
		char cpu_label[50];
		snprintf(cpu_label, 50, "CPU(%zu)", id);

		double activeness = cpu_stats_get_activeness(&(monitor->cpu_stats[id]));
		bool end_of_col = (id % 2 || id == (monitor->num_cpus - 1));

		printf("%s - %lf%%", cpu_label, activeness * 100.0);
		if (end_of_col) {
			printf("\n");
		} else {
			printf(" | ");
		}
	}

	printf("+-----------------------------+\n\n");
}

