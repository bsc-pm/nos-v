/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPUHWCOUNTERS_H
#define CPUHWCOUNTERS_H

#include "hwcounters/hwcounters.h"
#include "hwcounters/supportedhwcounters.h"

#if HAVE_PAPI
#include "hwcounters/papi/papicpuhwcounters.h"
#endif


typedef struct cpu_hwcounters {
#if HAVE_PAPI
	//! PAPI CPU hardware counters
	papi_cpuhwcounters_t papi_counters;
#endif
} cpu_hwcounters_t;


__internal void cpuhwcounters_initialize(cpu_hwcounters_t *counters)
{
#if HAVE_PAPI
	papi_cpuhwcounters_initialize(&(counters->papi_counters));
#endif
}

//! \brief Get the delta value of a HW counter
//! \param[in] counterType The type of counter to get the delta from
__internal uint64_t cpuhwcounters_get_delta(cpu_hwcounters_t *counters, enum counters_t type)
{
	if (type >= HWC_PAPI_MIN_EVENT && type <= HWC_PAPI_MAX_EVENT) {
#if HAVE_PAPI
		return papi_cpuhwcounters_get_delta(&(counters->papi_counters), type);
#endif
	}

	return 0;
}

#endif // CPUHWCOUNTERS_H
