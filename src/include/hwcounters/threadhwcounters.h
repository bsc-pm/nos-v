/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef THREADHWCOUNTERS_H
#define THREADHWCOUNTERS_H

#include "compiler.h"

#if HAVE_PAPI
#include "hwcounters/papi/papithreadhwcounters.h"
#endif


typedef struct thread_hwcounters {
#if HAVE_PAPI
	//! Thread-related hardware counters for the PAPI backend
	papi_threadhwcounters_t *papi_counters;
#endif
} thread_hwcounters_t;


//! \brief Allocate and initialize all backend objects
__internal void threadhwcounters_initialize(thread_hwcounters_t *counters);

//! \brief Destroy all backend objects
__internal void threadhwcounters_shutdown(thread_hwcounters_t *counters);

#endif // THREADHWCOUNTERS_H
