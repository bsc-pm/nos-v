/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPUHWCOUNTERS_H
#define CPUHWCOUNTERS_H

#include "compiler.h"
#include "hwcounters/supportedhwcounters.h"

#if HAVE_PAPI
#include "hwcounters/papi/papicpuhwcounters.h"
#endif


typedef struct cpu_hwcounters {
#if HAVE_PAPI
	//! PAPI CPU hardware counters
	papi_cpuhwcounters_t papi_counters;
#endif
	//! Due to C standard, structs cannot be empty, thus we leave an empty char
	char nothing;
} cpu_hwcounters_t;


//! \brief Initialize hardware counters for a CPU
__internal void cpuhwcounters_initialize(cpu_hwcounters_t *counters);

//! \brief Get the delta value of a HW counter
//! \param[in] counterType The type of counter to get the delta from
__internal uint64_t cpuhwcounters_get_delta(cpu_hwcounters_t *counters, enum counters_t type);

#endif // CPUHWCOUNTERS_H
