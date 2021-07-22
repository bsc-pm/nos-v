/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef THREADHWCOUNTERS_H
#define THREADHWCOUNTERS_H

struct papi_threadhwcounters;

typedef struct papi_threadhwcounters papi_threadhwcounters_t;

typedef struct thread_hwcounters
{
	//! Thread-related hardware counters for the PAPI backend
	papi_threadhwcounters_t *papi_counters;
} thread_hwcounters_t;


//! \brief Allocate and initialize all backend objects
__internal void threadhwcounters_initialize(thread_hwcounters_t *counters);

//! \brief Destroy all backend objects
__internal void threadhwcounters_shutdown(thread_hwcounters_t *counters);

#endif // THREADHWCOUNTERS_H
