/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef PAPITASKHWCOUNTERS_H
#define PAPITASKHWCOUNTERS_H

#include <stdint.h>

#include "compiler.h"
#include "hwcounters/supportedhwcounters.h"
#include "hwcounters/papi/papihwcounters.h"


// Initialize PAPI hardware counters for a task
__internal void papi_taskhwcounters_initialize(papi_taskhwcounters_t *counters);

// Read counters from an event set
__internal void papi_taskhwcounters_read_counters(papi_taskhwcounters_t *counters, int event_set);

// Get the delta value of a HW counter
__internal uint64_t papi_taskhwcounters_get_delta(papi_taskhwcounters_t *counters, enum counters_t type);

// Get a raw array of delta values of hwcounters
__internal uint64_t *papi_taskhwcounters_get_deltas(papi_taskhwcounters_t *counters);

// Get the accumulated value of a HW counter
__internal uint64_t papi_taskhwcounters_get_accumulated(papi_taskhwcounters_t *counters, enum counters_t type);

// Get a raw array of accumulated values of hwcounters
__internal uint64_t *papi_taskhwcounters_get_accumulation(papi_taskhwcounters_t *counters);

// Retreive the size needed for hardware counters
__internal size_t papi_taskhwcounters_get_alloc_size();

#endif // PAPITASKHWCOUNTERS_H
