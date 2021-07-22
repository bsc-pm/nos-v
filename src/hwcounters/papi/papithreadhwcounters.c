/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <papi.h>

#include "hwcounters/papi/papithreadhwcounters.h"


void papi_threadhwcounters_initialize(papi_threadhwcounters_t *counters)
{
	assert(counters != NULL);
	counters->event_set = PAPI_NULL;
}

int papi_threadhwcounters_get_eventset(papi_threadhwcounters_t *counters)
{
	assert(counters != NULL);
	return counters->event_set;
}

void papi_threadhwcounters_set_eventset(papi_threadhwcounters_t *counters, int set)
{
	assert(counters != NULL);
	counters->event_set = set;
}
