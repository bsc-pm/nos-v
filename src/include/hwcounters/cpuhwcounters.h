/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPUHWCOUNTERS_H
#define CPUHWCOUNTERS_H

#include <stdint.h>

#include "compiler.h"
#include "hwcounters/supportedhwcounters.h"

#if HAVE_PAPI
#include "hwcounters/papi/papicpuhwcounters.h"
#endif


typedef struct cpu_hwcounters {
#if HAVE_PAPI
	// PAPI CPU hardware counters
	papi_cpuhwcounters_t papi_counters;
#else
	// Due to C standard, structs cannot be empty, thus we leave an empty char
	char nothing;
#endif
} cpu_hwcounters_t;


// Initialize hardware counters for a CPU
__internal void cpuhwcounters_initialize(cpu_hwcounters_t *counters);

// Get the delta value of a HW counter
__internal uint64_t cpuhwcounters_get_delta(cpu_hwcounters_t *counters, enum counters_t type);

// Get a raw pointer to the array of delta HWCounter values
__internal uint64_t *cpuhwcounters_get_deltas(cpu_hwcounters_t *counters);

#endif // CPUHWCOUNTERS_H
