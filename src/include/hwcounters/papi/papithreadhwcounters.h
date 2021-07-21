/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef PAPITHREADHWCOUNTERS_H
#define PAPITHREADHWCOUNTERS_H

#include <papi.h>


typedef struct papi_threadhwcounters {
	//! The PAPI event set that must be read
	int event_set;
} papi_threadhwcounters_t;


__internal void papi_threadhwcounters_initialize(papi_threadhwcounters_t *counters)
{
	assert(counters != NULL);
	counters->event_set = PAPI_NULL;
}

__internal int papi_threadhwcounters_get_eventset(papi_threadhwcounters_t *counters)
{
	assert(counters != NULL);
	return counters->event_set;
}

__internal void papi_threadhwcounters_set_eventset(papi_threadhwcounters_t *counters, int set)
{
	assert(counters != NULL);
	counters->event_set = set;
}

#endif // PAPITHREADHWCOUNTERS_H
