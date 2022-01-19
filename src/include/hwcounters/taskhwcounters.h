/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef TASKHWCOUNTERS_H
#define TASKHWCOUNTERS_H

#include <stdbool.h>
#include <stdint.h>

#include "compiler.h"
#include "hwcounters/supportedhwcounters.h"

#if HAVE_PAPI
#include "hwcounters/papi/papitaskhwcounters.h"
#endif


typedef struct task_hwcounters {
	// Whether hardware counters are enabled for this task
	bool enabled;
#if HAVE_PAPI
	// PAPI Task hardware counters
	papi_taskhwcounters_t *papi_counters;
#endif
} task_hwcounters_t;


// Initialize and construct all the task counters with previously allocated space
__internal void taskhwcounters_initialize(task_hwcounters_t *counters, bool enabled);

// Check whether hardware counter monitoring is enabled for this task
__internal bool taskhwcounters_enabled(task_hwcounters_t *counters);

// Get the delta value of a HW counter
__internal uint64_t taskhwcounters_get_delta(task_hwcounters_t *counters, enum counters_t type);

// Get a raw pointer to the array of delta HWCounter values
__internal uint64_t *taskhwcounters_get_deltas(task_hwcounters_t *counters);

// Get the accumulated value of a HW counter
__internal uint64_t taskhwcounters_get_accumulated(task_hwcounters_t *counters, enum counters_t type);

// Get a raw pointer to the array of accumulated HWCounter values
__internal uint64_t *taskhwcounters_get_accumulation(task_hwcounters_t *counters);

// Get the size needed to construct all the structures for all backends
__internal size_t taskhwcounters_get_alloc_size();

#endif // TASKHWCOUNTERS_H
