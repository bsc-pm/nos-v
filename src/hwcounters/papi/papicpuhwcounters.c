/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2025 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <papi.h>
#include <string.h>

#include "common.h"
#include "hwcounters/papi/papicpuhwcounters.h"
#include "instr.h"


void papi_cpuhwcounters_initialize(papi_cpuhwcounters_t *counters)
{
	assert(counters != NULL);

	memset(&counters->delta, 0, sizeof(long long) * HWC_PAPI_NUM_EVENTS);
}

void papi_cpuhwcounters_read_counters(papi_cpuhwcounters_t *counters, int event_set)
{
	assert(counters != NULL);
	assert(event_set != PAPI_NULL);

	int ret = PAPI_read(event_set, counters->delta);
	if (ret != PAPI_OK) {
		nosv_abort("Code %d - Failed reading a PAPI event set - %s", ret, PAPI_strerror(ret));
	}

	ret = PAPI_reset(event_set);
	if (ret != PAPI_OK) {
		nosv_abort("Code %d - Failed resetting a PAPI event set - %s", ret, PAPI_strerror(ret));
	}

	/* Make sure the cast is legal */
	static_assert(sizeof(int64_t) == sizeof(long long));
	const size_t num_counters = papi_hwcounters_get_num_enabled_counters();
	instr_hwc_emit(num_counters, (int64_t *) counters->delta);
}

uint64_t papi_cpuhwcounters_get_delta(papi_cpuhwcounters_t *counters, enum counters_t type)
{
	assert(counters != NULL);
	assert(papi_hwcounters_counter_enabled(type));

	int id = papi_hwcounters_get_inner_identifier(type);
	assert(id >= 0 && (size_t) id < papi_hwcounters_get_num_enabled_counters());
	assert(counters->delta[id] >= 0);

	return (uint64_t) counters->delta[id];
}

uint64_t *papi_cpuhwcounters_get_deltas(papi_cpuhwcounters_t *counters)
{
	assert(counters != NULL);

	return (uint64_t *) counters->delta;
}

