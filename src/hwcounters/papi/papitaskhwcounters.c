/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <papi.h>

#include "common.h"
#include "hwcounters/papi/papitaskhwcounters.h"


void papi_taskhwcounters_initialize(papi_taskhwcounters_t *counters)
{
	assert(counters != NULL);

	const size_t num_counters = papi_hwcounters_get_num_enabled_counters();
	counters->delta = (long long *) ((char *) counters + sizeof(*counters));
	counters->accumulated = (long long *) ((char *) counters->delta + (num_counters * sizeof(long long)));

	memset(counters->delta, 0, sizeof(long long) * num_counters);
	memset(counters->accumulated, 0, sizeof(long long) * num_counters);
}

void papi_taskhwcounters_read_counters(papi_taskhwcounters_t *counters, int event_set)
{
	assert(counters != NULL);
	assert(event_set != PAPI_NULL);

	int ret = PAPI_read(event_set, counters->delta);
	assert(ret == PAPI_OK);

	ret = PAPI_reset(event_set);
	assert(ret == PAPI_OK);

	const size_t num_counters = papi_hwcounters_get_num_enabled_counters();
	for (size_t i = 0; i < num_counters; ++i) {
		counters->accumulated[i] += counters->delta[i];
	}
}

uint64_t papi_taskhwcounters_get_delta(papi_taskhwcounters_t *counters, enum counters_t type)
{
	assert(counters != NULL);
	assert(papi_hwcounters_counter_enabled(type));

	int inner_id = papi_hwcounters_get_inner_identifier(type);
	assert(inner_id >= 0 && (size_t) inner_id < papi_hwcounters_get_num_enabled_counters());
	assert(counters->delta[inner_id] >= 0);

	return (uint64_t) counters->delta[inner_id];
}

uint64_t *papi_taskhwcounters_get_deltas(papi_taskhwcounters_t *counters)
{
	assert(counters != NULL);

	return (uint64_t *) counters->delta;
}

uint64_t papi_taskhwcounters_get_accumulated(papi_taskhwcounters_t *counters, enum counters_t type)
{
	assert(counters != NULL);
	assert(papi_hwcounters_counter_enabled(type));

	int inner_id = papi_hwcounters_get_inner_identifier(type);
	assert(inner_id >= 0 && (size_t) inner_id < papi_hwcounters_get_num_enabled_counters());
	assert(counters->accumulated[inner_id] >= 0);

	return (uint64_t) counters->accumulated[inner_id];
}

uint64_t *papi_taskhwcounters_get_accumulation(papi_taskhwcounters_t *counters)
{
	assert(counters != NULL);

	return (uint64_t *) counters->accumulated;
}

size_t papi_taskhwcounters_get_alloc_size()
{
	return papi_hwcounters_get_num_enabled_counters() * 2 * sizeof(long long);
}
