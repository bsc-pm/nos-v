/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>

#include "hwcounters/hwcounters.h"
#include "hwcounters/taskhwcounters.h"


void taskhwcounters_initialize(task_hwcounters_t *counters, bool enabled)
{
	assert(counters != NULL);

	counters->enabled = enabled;
	if (enabled) {
#if HAVE_PAPI
		// Skip the size of "task_hwcounters_t"
		counters->papi_counters = (void *) ((char *) counters + sizeof(struct task_hwcounters));
		if (hwcounters_backend_enabled(PAPI_BACKEND)) {
			papi_taskhwcounters_initialize(counters->papi_counters);
		}
#endif
	}
}

bool taskhwcounters_enabled(task_hwcounters_t *counters)
{
	assert(counters != NULL);
	return counters->enabled;
}

uint64_t taskhwcounters_get_delta(task_hwcounters_t *counters, enum counters_t type)
{
	assert(counters != NULL);

	if (counters->enabled) {
		if (type >= HWC_PAPI_MIN_EVENT && type <= HWC_PAPI_MAX_EVENT) {
#if HAVE_PAPI
			papi_taskhwcounters_t *papi_counters = counters->papi_counters;
			if (hwcounters_backend_enabled(PAPI_BACKEND)) {
				return papi_taskhwcounters_get_delta(papi_counters, type);
			}
#endif
		}
	}

	return 0;
}

uint64_t *taskhwcounters_get_deltas(task_hwcounters_t *counters)
{
	assert(counters != NULL);

	if (counters->enabled) {
#if HAVE_PAPI
		papi_taskhwcounters_t *papi_counters = counters->papi_counters;
		if (hwcounters_backend_enabled(PAPI_BACKEND)) {
			return papi_taskhwcounters_get_deltas(papi_counters);
		}
#endif
	}

	return NULL;
}

uint64_t taskhwcounters_get_accumulated(task_hwcounters_t *counters, enum counters_t type)
{
	assert(counters != NULL);

	if (counters->enabled) {
		if (type >= HWC_PAPI_MIN_EVENT && type <= HWC_PAPI_MAX_EVENT) {
#if HAVE_PAPI
			papi_taskhwcounters_t *papi_counters = counters->papi_counters;
			if (hwcounters_backend_enabled(PAPI_BACKEND)) {
				return papi_taskhwcounters_get_accumulated(papi_counters, type);
			}
#endif
		}
	}

	return 0;
}

uint64_t *taskhwcounters_get_accumulation(task_hwcounters_t *counters)
{
	assert(counters != NULL);

	if (counters->enabled) {
#if HAVE_PAPI
		papi_taskhwcounters_t *papi_counters = counters->papi_counters;
		if (hwcounters_backend_enabled(PAPI_BACKEND)) {
			return papi_taskhwcounters_get_accumulation(papi_counters);
		}
#endif
	}

	return NULL;
}

size_t taskhwcounters_get_alloc_size()
{
	// The enabled boolean
	size_t total_size = sizeof(struct task_hwcounters);

	// Add the size needed by each backend
#if HAVE_PAPI
	// Add the PAPI struct's size plus the hardware counter space
	if (hwcounters_backend_enabled(PAPI_BACKEND)) {
		total_size += sizeof(papi_taskhwcounters_t) + papi_taskhwcounters_get_alloc_size();
	}
#endif

	return total_size;
}

