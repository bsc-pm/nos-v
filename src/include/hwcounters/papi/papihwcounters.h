/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef PAPIHWCOUNTERS_H
#define PAPIHWCOUNTERS_H

#include <stdbool.h>
#include <stddef.h>

#include "compiler.h"
#include "hwcounters/supportedhwcounters.h"


typedef struct papi_threadhwcounters {
	//! The PAPI event set that must be read
	int event_set;
} papi_threadhwcounters_t;

typedef struct papi_taskhwcounters {
	//! Arrays of regular HW counter deltas and accumulations
	long long *delta;
	long long *accumulated;
} papi_taskhwcounters_t;

typedef struct papi_cpu_hwcounters {
	//! Arrays of regular HW counter deltas
	long long delta[HWC_PAPI_NUM_EVENTS];
} papi_cpuhwcounters_t;


typedef struct papi_backend {
	//! Whether the PAPI HW Counter backend is enabled
	bool enabled;
	//! Whether the verbose mode is enabled
	bool verbose;
	//! A vector containing all the enabled PAPI event codes
	int enabled_event_codes[HWC_PAPI_NUM_EVENTS];
	//! The number of enabled counters
	size_t num_enabled_counters;
	//! Maps counters_t identifiers with the "inner PAPI id" (0..N)
	//!
	//! NOTE: This is an array with as many positions as possible counters in
	//! the PAPI backend (PAPI_NUM_EVENTS), even those that are disabled.
	//!   - If the value of a position is -1, there is no mapping (i.e., the
	//!     event is disabled).
	//!   - On the other hand, the value of a position is a mapping of a general
	//!     ID (HWC_PAPI_L1_DCM == 201) to the real position it should
	//!     occupy in a vector with only enabled PAPI events (0 for instance if
	//!     this is the only PAPI enabled event)
	//! id_table[HWC_PAPI_L1_DCM(201) - HWC_PAPI_MIN_EVENT(200)] = 0
	int id_table[HWC_PAPI_NUM_EVENTS];
} papi_backend_t;


//! \brief Initialize the PAPI backend
//! \param[in] verbose Whether verbose mode is enabled
//! \param[out] num_enabled_events A short which will be increased by the
//! number of enabled and available PAPI counters
//! \param[in,out] status_events A list of events which will be edited to
//! disable those that are unavailable
__internal void papi_hwcounters_initialize(
	bool verbose, short *num_enabled_counters,
	bool status_events[HWC_TOTAL_NUM_EVENTS]
);

//! \brief Retreive the mapping from a counters_t identifier to the inner
//! identifier of arrays with only enabled events
//! \param[in] type The identifier to translate
//! \return An integer with the relative position in arrays of only enabled
//! events or HWC_NULL_EVENT (-1) if this counter is disabled
__internal int papi_hwcounters_get_inner_identifier(enum counters_t type);

//! \brief Check whether a counter is enabled
//! \param[in] type The identifier to translate
__internal bool papi_hwcounters_counter_enabled(enum counters_t type);

//! \brief Get the number of enabled counters in the PAPI backend
__internal size_t papi_hwcounters_get_num_enabled_counters();

__internal void papi_hwcounters_thread_initialize(papi_threadhwcounters_t *thread_counters);

__internal void papi_hwcounters_thread_shutdown(papi_threadhwcounters_t *thread_counters);

__internal void papi_hwcounters_update_task_counters(
	papi_threadhwcounters_t *thread_counters,
	papi_taskhwcounters_t *task_counters
);

__internal void papi_hwcounters_update_runtime_counters(
	papi_cpuhwcounters_t *cpu_counters,
	papi_threadhwcounters_t *thread_counters
);

#endif // PAPIHWCOUNTERS_H
