/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "nosv-internal.h"
#include "config/config.h"
#include "hardware/threads.h"
#include "hwcounters/cpuhwcounters.h"
#include "hwcounters/hwcounters.h"
#include "hwcounters/taskhwcounters.h"
#include "hwcounters/threadhwcounters.h"

#if HAVE_PAPI
#include "hwcounters/papi/papihwcounters.h"
#endif


__internal hwcounters_backend_t hwcbackend;


void load_configuration(void)
{
	// Check which backend is enabled
	const char *hwcounters_envvar = nosv_config.hwcounters_backend;
	assert(hwcounters_envvar);

	if (!strcmp(hwcounters_envvar, "papi")) {
		hwcbackend.enabled[PAPI_BACKEND] = 1;
		hwcbackend.any_backend_enabled = 1;
	}

	// Check if verbose is enabled
	hwcbackend.verbose = nosv_config.hwcounters_verbose;

	// Get the list of enabled counters of each backend
	bool counter_added = 0;
	string_list_t hwcounters_list = nosv_config.hwcounters_papi_events;
	if (hwcounters_list.num_strings > 0) {
		for (int i = 0; i < hwcounters_list.num_strings; ++i) {
			for (int j = HWC_PAPI_MIN_EVENT; j <= HWC_PAPI_MAX_EVENT; ++j) {
				if (!strcmp(counter_descriptions[j - HWC_PAPI_MIN_EVENT].descr, hwcounters_list.strings[i])) {
					counter_added = 1;
					hwcbackend.status_counters[j] = 1;
				}
			}
		}
	}

	if (!counter_added) {
		nosv_warn("PAPI enabled but no counters enabled, disabling the backend!");
		hwcbackend.enabled[PAPI_BACKEND] = 0;
	}

	hwcbackend.any_backend_enabled = hwcbackend.enabled[PAPI_BACKEND];
}

//! \brief Check if multiple backends and/or other modules are enabled and incompatible
__internal void check_incompatibilities(void)
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

void hwcounters_initialize(void)
{
	// Initialize default values
	hwcbackend.verbose = 0;
	hwcbackend.any_backend_enabled = 0;
	hwcbackend.num_enabled_counters = 0;
	for (size_t i = 0; i < NUM_BACKENDS; ++i) {
		hwcbackend.enabled[i] = 0;
	}

	for (size_t i = 0; i < HWC_TOTAL_NUM_EVENTS; ++i) {
		hwcbackend.status_counters[i] = 0;
	}

	// Load the configuration to check which backends and events are enabled
	load_configuration();

	// Check if there's an incompatibility between backends
	check_incompatibilities();

	// If verbose is enabled and no backends are available, warn the user
	if (!hwcbackend.any_backend_enabled && hwcbackend.verbose) {
		nosv_warn("Hardware counters verbose mode enabled, but no backends available!");
	}

	// Initialize backends and keep track of the number of enabled counters
	if (hwcbackend.enabled[PAPI_BACKEND]) {
#if HAVE_PAPI
		papi_hwcounters_initialize(hwcbackend.verbose, (short *) &(hwcbackend.num_enabled_counters), hwcbackend.status_counters);
#else
		nosv_warn("PAPI library not found, disabling hardware counters");
		hwcbackend.enabled[PAPI_BACKEND] = 0;
#endif
	}

	if (hwcbackend.any_backend_enabled) {
		// After initializing all the backends, prepare a list of all the enabled types
		hwcbackend.enabled_counters = (enum counters_t *) malloc(sizeof(enum counters_t) * hwcbackend.num_enabled_counters);
		size_t id = 0;
		for (size_t i = 0; i < HWC_TOTAL_NUM_EVENTS; ++i) {
			if (hwcbackend.status_counters[i]) {
				assert(id < hwcbackend.num_enabled_counters);

				hwcbackend.enabled_counters[id] = i;
				++id;
			}
		}
	}
}

void hwcounters_shutdown(void)
{
	if (hwcbackend.any_backend_enabled) {
		assert(hwcbackend.enabled_counters);

		free(hwcbackend.enabled_counters);
	}

	if (hwcbackend.enabled[PAPI_BACKEND]) {
		hwcbackend.enabled[PAPI_BACKEND] = 0;
	}

	hwcbackend.any_backend_enabled = 0;
}

bool hwcounters_enabled(void)
{
	return hwcbackend.any_backend_enabled;
}

bool hwcounters_backend_enabled(enum backends_t backend)
{
	return hwcbackend.enabled[backend];
}

const bool *hwcounters_get_status_counters(void)
{
	return hwcbackend.status_counters;
}

const enum counters_t *hwcounters_get_enabled_counters(void)
{
	return hwcbackend.enabled_counters;
}

size_t hwcounters_get_num_enabled_counters(void)
{
	return hwcbackend.num_enabled_counters;
}

void hwcounters_thread_initialize(nosv_worker_t *thread)
{
	assert(thread != NULL);

	threadhwcounters_initialize(&(thread->counters));
}

void hwcounters_thread_shutdown(nosv_worker_t *thread)
{
	assert(thread != NULL);

	threadhwcounters_shutdown(&(thread->counters));
}

void hwcounters_task_created(nosv_task_t task, bool enabled)
{
	if (hwcbackend.any_backend_enabled) {
		assert(task != NULL);

		task_hwcounters_t *counters = task->counters;
		taskhwcounters_initialize(counters, enabled);
	}
}

void hwcounters_update_task_counters(nosv_task_t task)
{
	if (hwcbackend.any_backend_enabled) {
		assert(task != NULL);

		//! NOTE: We only read counters if they are enabled for the task. This
		//! is due to performance, and it leads to having incorrect CPU-wise
		//! counters if tasks are being skipped (as we're not resetting counters
		//! and they are all aggregated in the CPU counters)
		task_hwcounters_t *task_counters = task->counters;
		if (task_counters->enabled) {
			nosv_worker_t *thread = worker_current();
			assert(thread != NULL);

			__maybe_unused thread_hwcounters_t *thread_counters = &(thread->counters);
#if HAVE_PAPI
			if (hwcbackend.enabled[PAPI_BACKEND]) {
				assert(thread_counters != NULL);
				assert(task_counters != NULL);

				papi_threadhwcounters_t *papi_thread = thread_counters->papi_counters;
				papi_taskhwcounters_t *papi_task = task_counters->papi_counters;
				papi_hwcounters_update_task_counters(papi_thread, papi_task);
			}
#endif
		}
	}
}

void hwcounters_update_runtime_counters(void)
{
	if (hwcbackend.any_backend_enabled) {
		nosv_worker_t *thread = worker_current();
		assert(thread != NULL);

		cpu_t *cpu = thread->cpu;;
		if (cpu != NULL) {
			__maybe_unused cpu_hwcounters_t *cpu_counters = &(cpu->counters);
			__maybe_unused thread_hwcounters_t *thread_counters = &(thread->counters);
#if HAVE_PAPI
			if (hwcbackend.enabled[PAPI_BACKEND]) {
				assert(cpu_counters != NULL);
				assert(thread_counters != NULL);

				papi_cpuhwcounters_t *papi_cpu = &(cpu_counters->papi_counters);
				papi_threadhwcounters_t *papi_thread = thread_counters->papi_counters;
				papi_hwcounters_update_runtime_counters(papi_cpu, papi_thread);
			}
#endif
		}
	}
}

size_t hwcounters_get_task_size(void)
{
	return (hwcbackend.any_backend_enabled) ? taskhwcounters_get_alloc_size() : 0;
}
