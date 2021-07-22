/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef PAPITASKHWCOUNTERS_H
#define PAPITASKHWCOUNTERS_H

#include <stdint.h>

#include "compiler.h"
#include "hwcounters/supportedhwcounters.h"
#include "hwcounters/papi/papihwcounters.h"


//! \brief Initialize PAPI hardware counters for a task
__internal void papi_taskhwcounters_initialize(papi_taskhwcounters_t *counters);

//! \brief Read counters from an event set
//! \param[in] event_set The event set specified
__internal void papi_taskhwcounters_read_counters(papi_taskhwcounters_t *counters, int event_set);

//! \brief Get the delta value of a HW counter
//! \param[in] type The type of counter to get the delta from
__internal uint64_t papi_taskhwcounters_get_delta(papi_taskhwcounters_t *counters, enum counters_t type);

//! \brief Get the accumulated value of a HW counter
//! \param[in] type The type of counter to get the accumulation from
__internal uint64_t papi_taskhwcounters_get_accumulated(papi_taskhwcounters_t *counters, enum counters_t type);

//! \brief Retreive the size needed for hardware counters
__internal size_t papi_taskhwcounters_get_alloc_size();

#endif // PAPITASKHWCOUNTERS_H
