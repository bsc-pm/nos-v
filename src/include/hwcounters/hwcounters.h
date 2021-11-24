/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef HWCOUNTERS_H
#define HWCOUNTERS_H

#include <stddef.h>

#include "compiler.h"
#include "nosv.h"
#include "supportedhwcounters.h"
#include "hardware/threads.h"


typedef struct hwcounters_backend {
	// Whether the verbose mode is enabled
	short verbose;
	// Whether there is at least one enabled backend
	short any_backend_enabled;
	// Whether each backend is enabled
	short enabled[NUM_BACKENDS];
	// An array in which each position tells whether the 'i-th' event is enabled
	enum counters_t enabled_counters[HWC_MAX_EVENT_ID + 1];
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
__internal short hwcounters_enabled();

// Check whether a backend is enabled
__internal short hwcounters_backend_enabled(enum backends_t backend);

// Out of all the supported events, get the currently enabled ones
__internal const enum counters_t *hwcounters_get_enabled_counters();

// Get the number of supported and enabled counters
__internal size_t hwcounters_get_num_enabled_counters();

// Initialize hardware counter structures for a new thread
__internal void hwcounters_thread_initialize(nosv_worker_t *thread);

// Destroy the hardware counter structures of a thread
__internal void hwcounters_thread_shutdown();

// Initialize hardware counter structures for a task
__internal void hwcounters_task_created(nosv_task_t task, short enabled);

// Read and update hardware counters for a task
__internal void hwcounters_update_task_counters(nosv_task_t task);

// Read and update hardware counters for the runtime (current CPU)
__internal void hwcounters_update_runtime_counters();

// Get the size needed to allocate task hardware counters
__internal size_t hwcounters_get_task_size();

#endif // HWCOUNTERS_H
