/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <stdio.h>
#include <stdlib.h>

#include "generic/spinlock.h"
#include "memory/sharedmemory.h"
#include "monitoring/monitoring.h"


// The shared monitoring manager
__internal monitoring_manager_t *monitor;
__internal short monitoring_enabled;

void monitoring_init(short initialize)
{
	// First check if Monitoring is enabled for this process
	monitoring_enabled = 0;
	char *monitoring_envvar = getenv("MONITORING_ENABLE");
	if (monitoring_envvar) {
		monitoring_enabled = (atoi(monitoring_envvar) == 1);
	}

	// Regardless of enable, check if we must allocate and initialize the shared modules
	if (!initialize) {
		monitor = (monitoring_manager_t *) st_config.config->monitoring_ptr;
		return;
	}

	// Allocate the monitoring manager
	monitor = (monitoring_manager_t *) salloc(sizeof(monitoring_manager_t), -1);
	st_config.config->monitoring_ptr = monitor;
	assert(monitor != NULL);

	// Allocate the CPU monitor
	monitor->cpumonitor = (cpumonitor_t *) salloc(sizeof(cpumonitor_t), -1);
	assert(monitor->cpumonitor != NULL);

	// Initialize the CPU monitor
	cpumonitor_initialize(monitor->cpumonitor);

	// Initialize the spinlock
	nosv_spin_init(&monitor->lock);

	// Check if verbosity is enabled
	monitor->verbose = 0;
	if (monitoring_enabled) {
		char *monitoring_verbose = getenv("MONITORING_VERBOSE");
		if (monitoring_verbose) {
			monitor->verbose = (atoi(monitoring_verbose) == 1);
		}
	}
}

void monitoring_shutdown()
{
	if (monitoring_enabled) {
		monitoring_display_stats();
	}
}

short monitoring_is_enabled()
{
	return monitoring_enabled;
}

void monitoring_display_stats()
{
	assert(monitoring_enabled);
	assert(monitor != NULL);
	assert(monitor->cpumonitor != NULL);

	// Retrieve statistics from every monitor
	if (monitor->verbose) {
		cpumonitor_statistics(monitor->cpumonitor);
		taskmonitor_statistics();
	}
}

void monitoring_task_created(nosv_task_t task)
{
}

void monitoring_type_created(nosv_task_type_t type)
{
}

void monitoring_task_changed_status(nosv_task_t task, enum monitoring_status_t status)
{
}

void monitoring_task_completed(nosv_task_t task)
{
}

void monitoring_task_finished(nosv_task_t task)
{
}

size_t monitoring_get_allocation_size()
{
	if (monitoring_enabled) {
		return sizeof(taskstatistics_t);
	} else {
		return 0;
	}
}

void monitoring_cpu_idle(int cpu_id)
{
	if (monitoring_enabled) {
		assert(monitor != NULL);

		cpumonitor_cpu_idle(monitor->cpumonitor, cpu_id);
	}
}

void monitoring_cpu_active(int cpu_id)
{
	if (monitoring_enabled) {
		assert(monitor != NULL);

		cpumonitor_cpu_active(monitor->cpumonitor, cpu_id);
	}
}

