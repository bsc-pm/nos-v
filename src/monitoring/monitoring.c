/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#include <stdio.h>
#include <stdlib.h>

#include "config/config.h"
#include "generic/spinlock.h"
#include "memory/sharedmemory.h"
#include "monitoring/monitoring.h"


// The shared monitoring manager
__internal monitoring_manager_t *monitor;
__internal bool monitoring_enabled;

void monitoring_init(bool initialize)
{
	// First check if Monitoring is enabled for this process
	monitoring_enabled = nosv_config.monitoring_enabled;

	// Regardless of enable, check if we must allocate and initialize the shared modules
	if (!initialize) {
		monitor = (monitoring_manager_t *) st_config.config->monitoring_ptr;
		return;
	}

	if (monitoring_enabled) {
		// Allocate the monitoring manager
		monitor = (monitoring_manager_t *) salloc(sizeof(monitoring_manager_t), -1);
		st_config.config->monitoring_ptr = monitor;
		assert(monitor != NULL);

		// Allocate the CPU monitor
		monitor->cpumonitor = (cpumonitor_t *) salloc(sizeof(cpumonitor_t), -1);
		assert(monitor->cpumonitor != NULL);

		// Initialize the CPU monitor
		cpumonitor_initialize(monitor->cpumonitor);

		// Check if verbosity is enabled
		monitor->verbose = nosv_config.monitoring_verbose;
	}
}

void monitoring_free(void)
{
	if (monitoring_enabled) {
		cpumonitor_free(monitor->cpumonitor);

		sfree(monitor->cpumonitor, sizeof(cpumonitor_t), -1);
		sfree(monitor, sizeof(monitoring_manager_t), -1);
	}
}

bool monitoring_is_enabled(void)
{
	return monitoring_enabled;
}

void monitoring_display_stats(void)
{
	if (monitoring_enabled) {
		// Retrieve statistics from every monitor
		if (monitor->verbose) {
			cpumonitor_statistics(monitor->cpumonitor);
			taskmonitor_statistics();
		}
	}
}

void monitoring_task_created(nosv_task_t task)
{
	if (monitoring_enabled) {
		taskmonitor_task_created(task);
	}
}

void monitoring_task_submitted(nosv_task_t task)
{
	if (monitoring_enabled) {
		taskmonitor_task_submitted(task);
	}
}

void monitoring_type_created(nosv_task_type_t type)
{
	if (monitoring_enabled) {
		assert(type != NULL);

		void *alloc_address = (char *) type->stats + sizeof(tasktype_stats_t);
		tasktype_stats_init(type->stats, alloc_address);
	}
}

void monitoring_task_changed_status(nosv_task_t task, monitoring_status_t status)
{
	if (monitoring_enabled) {
		// Start timing for the appropriate stopwatch
		taskmonitor_task_started(task, status);
	}
}

void monitoring_task_completed(nosv_task_t task)
{
	if (monitoring_enabled) {
		// Mark task as completely executed
		taskmonitor_task_completed(task);
	}
}

size_t monitoring_get_task_size(void)
{
	if (monitoring_enabled) {
		return sizeof(task_stats_t) + task_stats_get_allocation_size();
	} else {
		return 0;
	}
}

size_t monitoring_get_tasktype_size(void)
{
	if (monitoring_enabled) {
		return sizeof(tasktype_stats_t) + tasktype_stats_get_allocation_size();
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

