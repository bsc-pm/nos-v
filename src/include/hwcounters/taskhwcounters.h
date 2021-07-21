/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef TASKHWCOUNTERS_H
#define TASKHWCOUNTERS_H

#include <stdint.h>

#include "hwcounters/hwcounters.h"
#include "hwcounters/supportedhwcounters.h"

#if HAVE_PAPI
#include "hwcounters/papi/papitaskhwcounters.h"
#endif


typedef struct task_hwcounters {
	//! Whether hardware counters are enabled for this task
	short enabled;
#if HAVE_PAPI
	//! PAPI Task hardware counters
	papi_taskhwcounters_t *papi_counters;
#endif
} task_hwcounters_t;


//! \brief Initialize and construct all the task counters with previously allocated space
//! \param[in,out] counters The address of the task counters
//! \param[in] enabled Whether hardware counters are enabled for the task
__internal void taskhwcounters_initialize(task_hwcounters_t *counters, short enabled)
{
	assert(counters != NULL);

	counters->enabled = enabled;
	if (enabled) {
		// Skip the "enabled" boolean from "task_hwcounters_t"
		counters->papi_counters = (void *) ((char *) counters + sizeof(short));
#if HAVE_PAPI
		if (hwcounters_backend_enabled(PAPI_BACKEND)) {
			papi_taskhwcounters_initialize(counters->papi_counters);
		}
#endif
	}
}

//! \brief Check whether hardware counter monitoring is enabled for this task
__internal short taskhwcounters_enabled(task_hwcounters_t *counters)
{
	assert(counters != NULL);
	return counters->enabled;
}

//! \brief Return the PAPI counters of the task (if it is enabled) or nullptr
__internal void *taskhwcounters_get_papi_counters(task_hwcounters_t *counters)
{
	assert(counters != NULL);

	if (counters->enabled) {
		if (hwcounters_backend_enabled(PAPI_BACKEND)) {
			return (void *) counters->papi_counters;
		}
	}

	return NULL;
}

//! \brief Get the delta value of a HW counter
//! \param[in] type The type of counter to get the delta from
__internal uint64_t taskhwcounters_get_delta(task_hwcounters_t *counters, enum counters_t type)
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

//! \brief Get the accumulated value of a HW counter
//! \param[in] type The type of counter to get the accumulation from
__internal uint64_t taskhwcounters_get_accumulated(task_hwcounters_t *counters, enum counters_t type)
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

//! \brief Get the size needed to construct all the structures for all backends
__internal size_t taskhwcounters_get_alloc_size()
{
	// The enabled boolean and the backend pointers
	size_t total_size = sizeof(short) + sizeof(papi_taskhwcounters_t *);

	// Add the size needed by each backend
#if HAVE_PAPI
	if (hwcounters_backend_enabled(PAPI_BACKEND)) {
		total_size += sizeof(papi_taskhwcounters_t) + papi_taskhwcounters_get_alloc_size();
	}
#endif

	return total_size;
}

#endif // TASKHWCOUNTERS_H
