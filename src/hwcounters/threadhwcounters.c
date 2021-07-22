/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <stdlib.h>

#include "hwcounters/hwcounters.h"
#include "hwcounters/supportedhwcounters.h"
#include "hwcounters/threadhwcounters.h"


void threadhwcounters_initialize(thread_hwcounters_t *counters)
{
#if HAVE_PAPI
	if (hwcounters_backend_enabled(PAPI_BACKEND)) {
		assert(counters != NULL);

		counters->papi_counters = (papi_threadhwcounters_t *) malloc(sizeof(papi_threadhwcounters_t));
		assert(counters->papi_counters != NULL);

		papi_threadhwcounters_initialize(counters->papi_counters);
	}
#endif
}

void threadhwcounters_shutdown(thread_hwcounters_t *counters)
{
#if HAVE_PAPI
	if (hwcounters_backend_enabled(PAPI_BACKEND)) {
		assert(counters != NULL);
		assert(counters->papi_counters != NULL);

		free(counters->papi_counters);
	}
#endif
}
