/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef HWCOUNTERS_H
#define HWCOUNTERS_H

#include <stddef.h>

#include "compiler.h"
#include "nosv.h"
#include "supportedhwcounters.h"
#include "hardware/threads.h"


typedef struct hwcounters_backend {
	// Whether the verbose mode is enabled
	short verbose;
	// Whether there is at least one enabled backend
	short any_backend_enabled;
	//! Whether each backend is enabled
	short enabled[NUM_BACKENDS];
	//! An array in which each position tells whether the 'i-th' event is enabled
	enum counters_t enabled_counters[HWC_MAX_EVENT_ID];
	//! The number of enabled counters
	size_t num_enabled_counters;
} hwcounters_backend_t;


//! \brief Load backends and counter configuration
__internal void load_configuration();

//! \brief Check if multiple backends and/or other modules are enabled and incompatible
__internal void check_incompatibilities();

//! \brief Initialize the hardware counters API
__internal void hwcounters_initialize();

//! \brief Shutdown the hardware counters API
__internal void hwcounters_shutdown();

//! \brief Check whether any backend is enabled
__internal short hwcounters_enabled();

//! \brief Check whether a backend is enabled
//! \param[in] backend The backend's id
__internal short hwcounters_backend_enabled(enum backends_t backend);

//! \brief Out of all the supported events, get the currently enabled ones
__internal const enum counters_t *hwcounters_get_enabled_counters();

//! \brief Get the number of supported and enabled counters
__internal size_t hwcounters_get_num_enabled_counters();

//! \brief Initialize hardware counter structures for a new thread
//! \param[in,out] thread The current worker thread
__internal void hwcounters_thread_initialize(nosv_worker_t *thread);

//! \brief Destroy the hardware counter structures of a thread
__internal void hwcounters_thread_shutdown();

//! \brief Initialize hardware counter structures for a task
//! \param[out] task The task to create structures for
//! \param[in] enabled Whether to create structures and monitor this task
__internal void hwcounters_task_created(nosv_task_t task, short enabled);

//! \brief Read and update hardware counters for a task
//! This function should be called right before a task stops/ends executing
//! its user code, in all the runtime points where it does, so that counters
//! can be read and accumulated. After a tasks stops, the subsequent values
//! accumulated in the counters belong to runtime-related operations
//! \param[out] task The task to read hardware counters for
__internal void hwcounters_update_task_counters(nosv_task_t task);

//! \brief Read and update hardware counters for the runtime (current CPU)
//! This function should be called right before starting the execution of a
//! task, so that the counters up to that point are assigned to the CPU
//! executing runtime code and they are not accumulated into the executing task
__internal void hwcounters_update_runtime_counters();

//! \brief Get the size needed to allocate task hardware counters
__internal size_t hwcounters_get_task_size();

#endif // HWCOUNTERS_H
