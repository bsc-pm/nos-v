/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef PAPITHREADHWCOUNTERS_H
#define PAPITHREADHWCOUNTERS_H

#include "compiler.h"
#include "hwcounters/papi/papihwcounters.h"


__internal void papi_threadhwcounters_initialize(papi_threadhwcounters_t *counters);

__internal int papi_threadhwcounters_get_eventset(papi_threadhwcounters_t *counters);

__internal void papi_threadhwcounters_set_eventset(papi_threadhwcounters_t *counters, int set);

#endif // PAPITHREADHWCOUNTERS_H
