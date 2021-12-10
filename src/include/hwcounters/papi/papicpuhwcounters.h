/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef PAPICPUHWCOUNTERS_H
#define PAPICPUHWCOUNTERS_H

#include <stdint.h>

#include "compiler.h"
#include "hwcounters/supportedhwcounters.h"
#include "hwcounters/papi/papihwcounters.h"


__internal void papi_cpuhwcounters_initialize(papi_cpuhwcounters_t *counters);

__internal void papi_cpuhwcounters_read_counters(papi_cpuhwcounters_t *counters, int event_set);

__internal uint64_t papi_cpuhwcounters_get_delta(papi_cpuhwcounters_t *counters, enum counters_t type);

__internal uint64_t *papi_cpuhwcounters_get_deltas(papi_cpuhwcounters_t *counters);

#endif // PAPICPUHWCOUNTERS_H
