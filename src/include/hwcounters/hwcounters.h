/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef HWCOUNTERS_H
#define HWCOUNTERS_H

#include <stdbool.h>
#include <stddef.h>

#include "compiler.h"
#include "nosv.h"
#include "supportedhwcounters.h"
#include "hardware/threads.h"


typedef struct hwcounters_backend {
	// Whether the verbose mode is enabled
	bool verbose;
	// Whether there is at least one enabled backend
	bool any_backend_enabled;
	// Whether each backend is enabled
	bool enabled[NUM_BACKENDS];
	// An array in which each position tells whether the 'i-th' counter type is
	// enabled. This array is as long as the full list of supported counters
	bool status_counters[HWC_TOTAL_NUM_EVENTS];
	// An array as long as the number of enabled counters (num_enabled_counters),
	// which contains only the enabled hardware counter types
	enum counters_t *enabled_counters;
	// The number of enabled counters
	size_t num_enabled_counters;
} hwcounters_backend_t;


// Load backends and counter configuration
__internal void load_configuration();

// Check if multiple backends and/or other modules are enabled and incompatible
__internal void check_incompatibilities();

// Initialize the hardware counters API
__internal void hwcounters_initialize();

// Shutdown the hardware counters API
__internal void hwcounters_shutdown();

// Check whether any backend is enabled
__internal bool hwcounters_enabled();

// Check whether a backend is enabled
__internal bool hwcounters_backend_enabled(enum backends_t backend);

// Get an array which tells whether each counter is enabled
__internal const bool *hwcounters_get_status_counters();

// Get an array of enabled counter types
__internal const enum counters_t *hwcounters_get_enabled_counters();

// Get the number of supported and enabled counters
__internal size_t hwcounters_get_num_enabled_counters();

// Initialize hardware counter structures for a new thread
__internal void hwcounters_thread_initialize(nosv_worker_t *thread);

// Destroy the hardware counter structures of a thread
__internal void hwcounters_thread_shutdown();

// Initialize hardware counter structures for a task
__internal void hwcounters_task_created(nosv_task_t task, bool enabled);

// Read and update hardware counters for a task
__internal void hwcounters_update_task_counters(nosv_task_t task);

// Read and update hardware counters for the runtime (current CPU)
__internal void hwcounters_update_runtime_counters();

// Get the size needed to allocate task hardware counters
__internal size_t hwcounters_get_task_size();

#endif // HWCOUNTERS_H
