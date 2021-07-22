/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <papi.h>

#include "hwcounters/papi/papicpuhwcounters.h"


void papi_cpuhwcounters_initialize(papi_cpuhwcounters_t *counters)
{
	assert(counters != NULL);

	for (size_t i = 0; i < HWC_PAPI_NUM_EVENTS; ++i) {
		counters->delta[i] = 0;
	}
}

void papi_cpuhwcounters_read_counters(papi_cpuhwcounters_t *counters, int event_set)
{
	assert(counters != NULL);
	assert(event_set != PAPI_NULL);

	int ret = PAPI_read(event_set, counters->delta);
	if (ret != PAPI_OK) {
		// TODO: fail: (ret, " when reading a PAPI event set - ", PAPI_strerror(ret));
	}

	ret = PAPI_reset(event_set);
	if (ret != PAPI_OK) {
		// TODO: fail: (ret, " when resetting a PAPI event set - ", PAPI_strerror(ret));
	}
}

uint64_t papi_cpuhwcounters_get_delta(papi_cpuhwcounters_t *counters, enum counters_t type)
{
	assert(counters != NULL);
	assert(papi_hwcounters_counter_enabled(type));

	int id = papi_hwcounters_get_inner_identifier(type);
	assert(id >= 0 && (size_t) id < papi_hwcounters_get_num_enabled_counters());

	return (uint64_t) counters->delta[id];
}
