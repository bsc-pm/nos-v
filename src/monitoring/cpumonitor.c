/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <stdio.h>

#include "hardware/cpus.h"
#include "monitoring/cpumonitor.h"


void cpumonitor_initialize(cpumonitor_t *monitor)
{
	assert(monitor != NULL);

	monitor->num_cpus = (size_t) cpus_count();
	monitor->cpu_stats = (cpustatistics_t *) salloc(sizeof(cpustatistics_t) * monitor->num_cpus, -1);
	assert(monitor->cpu_stats != NULL);

	// Initialize the fields of each CPU statistics
	for (size_t id = 0; id < monitor->num_cpus; ++id) {
		cpustatistics_init(&(monitor->cpu_stats[id]));
	}
}

void cpumonitor_shutdown(cpumonitor_t *monitor)
{
	assert(monitor != NULL);

	sfree(monitor->cpu_stats, sizeof(cpustatistics_t) * monitor->num_cpus, -1);
}

void cpumonitor_cpu_active(cpumonitor_t *monitor, int cpu_id)
{
	assert(monitor != NULL);
	assert(monitor->cpu_stats != NULL);
	assert((size_t) cpu_id < monitor->num_cpus);

	cpustatistics_active(&(monitor->cpu_stats[cpu_id]));
}

void cpumonitor_cpu_idle(cpumonitor_t *monitor, int cpu_id)
{
	assert(monitor != NULL);
	assert(monitor->cpu_stats != NULL);
	assert((size_t) cpu_id < monitor->num_cpus);

	cpustatistics_idle(&(monitor->cpu_stats[cpu_id]));
}

float cpumonitor_get_activeness(cpumonitor_t *monitor, int cpu_id)
{
	assert(monitor != NULL);
	assert(monitor->cpu_stats != NULL);
	assert((size_t) cpu_id < monitor->num_cpus);

	return cpustatistics_get_activeness(&(monitor->cpu_stats[cpu_id]));
}

float cpumonitor_get_total_activeness(cpumonitor_t *monitor)
{
	assert(monitor != NULL);
	assert(monitor->cpu_stats != NULL);

	float total_activeness = 0.0;
	for (size_t id = 0; monitor->num_cpus; ++id) {
		total_activeness += cpustatistics_get_activeness(&(monitor->cpu_stats[id]));
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

		float activeness = cpustatistics_get_activeness(&(monitor->cpu_stats[id]));
		short end_of_col = (id % 2 || id == (monitor->num_cpus - 1));

		printf("%s - %lf%%", cpu_label, activeness * 100.0);
		if (end_of_col) {
			printf("\n");
		} else {
			printf(" | ");
		}
	}

	printf("+-----------------------------+\n\n");
}

