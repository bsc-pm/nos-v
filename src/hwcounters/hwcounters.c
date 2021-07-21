/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

#include "hardware/threads.h"
#include "hwcounters/hwcounters.h"
#include "hwcounters/threadhwcounters.h"


// Whether the verbose mode is enabled
__internal short verbose;
// The underlying PAPI backend
// __internal something_t papi_backend;
// Whether there is at least one enabled backend
__internal short any_backend_enabled;
//! Whether each backend is enabled
__internal short enabled[NUM_BACKENDS];
//! Enabled counters by the user
__internal enum counters_t enabled_counters[HWC_TOTAL_NUM_EVENTS];
//! The number of enabled counters
__internal size_t num_enabled_counters;


void load_configuration()
{
	// Check which backend is enabled
	char *hwcounters_envvar = getenv("HWCOUNTERS_BACKEND");
	if (hwcounters_envvar) {
		if (!strcmp(hwcounters_envvar, "papi")) {
			enabled[PAPI_BACKEND] = 1;
			any_backend_enabled = 1;
		}
	}

	// Check if verbose is enabled
	hwcounters_envvar = getenv("HWCOUNTERS_VERBOSE");
	if (hwcounters_envvar) {
		verbose = (atoi(hwcounters_envvar) == 1);
	}

	// Get the list of enabled counters of each backend
	short counter_added = 0;
	hwcounters_envvar = getenv("HWCOUNTERS_PAPI");
	if (hwcounters_envvar) {
		char *counter_label = strtok(hwcounters_envvar, ",");
		while (counter_label != NULL) {
			for (short i = HWC_PAPI_MIN_EVENT; i <= HWC_PAPI_MAX_EVENT; ++i) {
				if (!strcmp(counter_descriptions[i - HWC_PAPI_MIN_EVENT].descr, counter_label)) {
					counter_added = 1;
					enabled_counters[i - HWC_PAPI_MIN_EVENT] = 1;
				}
			}

			counter_label = strtok(NULL, ",");
		}
	}

	if (!counter_added) {
		// TODO: Warn "PAPI enabled but no counters are enabled in the config file, disabling this backend"
	}

	any_backend_enabled = enabled[PAPI_BACKEND];
}

//! \brief Check if multiple backends and/or other modules are enabled and incompatible
__internal void check_incompatibilities()
{
	// TODO: When Instrumentation is implemented
	// If extrae is enabled, disable PAPI to avoid hardware counter collisions
// #ifdef EXTRAE_ENABLED
// 	if (enabled[PAPI_BACKEND]) {
// 		// NOTE: Warn?
// 		enabled[PAPI_BACKEND] = 0;
// 	}
// #endif
}

void hwcounters_initialize()
{
	// Initialize default values
	verbose = 0;
	any_backend_enabled = 0;
	num_enabled_counters = 0;
	for (size_t i = 0; i < NUM_BACKENDS; ++i) {
		enabled[i] = 0;
	}

	for (size_t i = 0; i < HWC_TOTAL_NUM_EVENTS; ++i) {
		enabled_counters[i] = 0;
	}

	// Load the configuration to check which backends and events are enabled
	load_configuration();

	// Check if there's an incompatibility between backends
	check_incompatibilities();

	// If verbose is enabled and no backends are available, warn the user
	if (!any_backend_enabled && verbose) {
		// TODO: Warn
		// "Hardware Counters verbose mode enabled but no backends available!"
	}

	// Initialize backends and keep track of the number of enabled counters
	if (enabled[PAPI_BACKEND]) {
#if HAVE_PAPI
		// TODO: Init PAPI
		//papi_backend = new PAPIHardwareCounters(
		//	&enabled_counters,
		//	&num_enabled_counters
		//);
#else
		// TODO: Warn: "PAPI library not found, disabling hardware counters."
		enabled[PAPI_BACKEND] = 0;
#endif
	}
}

void hwcounters_shutdown()
{
	if (enabled[PAPI_BACKEND]) {
		// TODO: assert(papi_backend != NULL);

		// TODO: delete _papiBackend;
		// TODO: papi_backend = NULL;
		enabled[PAPI_BACKEND] = 0;
	}

	any_backend_enabled = 0;
}

short hwcounters_enabled()
{
	return any_backend_enabled;
}

short hwcounters_backend_enabled(enum backends_t backend)
{
	return enabled[backend];
}

const enum counters_t *hwcounters_get_enabled_counters()
{
	return enabled_counters;
}

size_t hwcounters_get_num_enabled_counters()
{
	return num_enabled_counters;
}

void hwcounters_thread_initialized()
{
	nosv_worker_t *thread = worker_current();
	assert(thread != NULL);

	threadhwcounters_initialize(&(thread->counters));
}

void hwcounters_thread_shutdown()
{
	nosv_worker_t *thread = worker_current();
	assert(thread != NULL);

	threadhwcounters_initialize(&(thread->counters));
}

void hwcounters_task_created(nosv_task_t task, short enabled)
{
	if (any_backend_enabled) {
		assert(task != NULL);

		task_hwcounters_t *counters = (task_hwcounters_t *) task->counters;
		taskhwcounters_initialize(counters, enabled);
	}
}

void hwcounters_update_task_counters(nosv_task_t task)
{
	if (any_backend_enabled) {
// 		WorkerThread *thread = WorkerThread::getCurrentWorkerThread();
// 		assert(thread != nullptr);
		assert(task != NULL);

// 		ThreadHardwareCounters &threadCounters = thread->getHardwareCounters();
// 		TaskHardwareCounters &taskCounters = task->getHardwareCounters();
// 		if (_enabled[HWCounters::PAPI_BACKEND]) {
// 			assert(_papiBackend != nullptr);
//
// 			_papiBackend->updateTaskCounters(
// 				threadCounters.getPAPICounters(),
// 				taskCounters.getPAPICounters()
// 			);
// 		}
	}
}

void hwcounters_update_runtime_counters()
{
	if (any_backend_enabled) {
// 		WorkerThread *thread = WorkerThread::getCurrentWorkerThread();
// 		assert(thread != nullptr);
//
// 		CPU *cpu = thread->getComputePlace();
// 		assert(cpu != nullptr);
//
// 		CPUHardwareCounters &cpuCounters = cpu->getHardwareCounters();
// 		ThreadHardwareCounters &threadCounters = thread->getHardwareCounters();
// 		if (_enabled[HWCounters::PAPI_BACKEND]) {
// 			assert(_papiBackend != nullptr);
//
// 			_papiBackend->updateRuntimeCounters(
// 				cpuCounters.getPAPICounters(),
// 				threadCounters.getPAPICounters()
// 			);
// 		}
	}
}

size_t hwcounters_get_task_size()
{
	return (any_backend_enabled) ? taskhwcounters_get_alloc_size() : 0;
}
