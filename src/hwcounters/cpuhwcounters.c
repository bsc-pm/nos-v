/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include "hwcounters/cpuhwcounters.h"
#include "hwcounters/hwcounters.h"


void cpuhwcounters_initialize(cpu_hwcounters_t *counters)
{
#if HAVE_PAPI
	papi_cpuhwcounters_initialize(&counters->papi_counters);
#endif
}

uint64_t cpuhwcounters_get_delta(cpu_hwcounters_t *counters, enum counters_t type)
{
	if (type >= HWC_PAPI_MIN_EVENT && type <= HWC_PAPI_MAX_EVENT) {
#if HAVE_PAPI
		return papi_cpuhwcounters_get_delta(&counters->papi_counters, type);
#endif
	}

	return 0;
}

uint64_t *cpuhwcounters_get_deltas(cpu_hwcounters_t *counters)
{
#if HAVE_PAPI
	return papi_cpuhwcounters_get_deltas(&counters->papi_counters);
#endif

	return NULL;
}
