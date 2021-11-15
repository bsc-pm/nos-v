/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef TASKHWCOUNTERS_H
#define TASKHWCOUNTERS_H

#include <stdint.h>

#include "compiler.h"
#include "hwcounters/supportedhwcounters.h"

#if HAVE_PAPI
#include "hwcounters/papi/papitaskhwcounters.h"
#endif


typedef struct task_hwcounters {
	//! Whether hardware counters are enabled for this task
	short enabled;
#if HAVE_PAPI
	//! PAPI Task hardware counters
	papi_taskhwcounters_t *papi_counters;
#endif
} task_hwcounters_t;


//! \brief Initialize and construct all the task counters with previously allocated space
//! \param[in,out] counters A pointer to the task's counters
//! \param[in] enabled Whether hardware counters are enabled for the task
__internal void taskhwcounters_initialize(task_hwcounters_t *counters, short enabled);

//! \brief Check whether hardware counter monitoring is enabled for this task
//! \param[in,out] counters A pointer to the task's counters
__internal short taskhwcounters_enabled(task_hwcounters_t *counters);

//! \brief Get the delta value of a HW counter
//! \param[in] type The type of counter to get the delta from
__internal uint64_t taskhwcounters_get_delta(task_hwcounters_t *counters, enum counters_t type);

//! \brief Get the accumulated value of a HW counter
//! \param[in] type The type of counter to get the accumulation from
__internal uint64_t taskhwcounters_get_accumulated(task_hwcounters_t *counters, enum counters_t type);

//! \brief Get the size needed to construct all the structures for all backends
__internal size_t taskhwcounters_get_alloc_size();

#endif // TASKHWCOUNTERS_H
