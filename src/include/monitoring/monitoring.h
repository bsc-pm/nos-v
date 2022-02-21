/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef MONITORING_H
#define MONITORING_H

#include <stdbool.h>
#include <stddef.h>

#include "compiler.h"
#include "cpumonitor.h"
#include "monitoringsupport.h"
#include "nosv.h"
#include "taskmonitor.h"
#include "taskstats.h"


typedef struct monitoring_manager {
	//! Whether verbosity for monitoring enabled
	bool verbose;
	//! A monitor that handles CPU statistics
	cpumonitor_t *cpumonitor;
} monitoring_manager_t;

//! \brief Initialize monitoring structures
//! \param[in] initialize Whether this is the first time (process) initializing monitoring
__internal void monitoring_init(bool initialize);

//! \brief Shutdown monitoring
__internal void monitoring_shutdown(void);

//! \brief Check whether monitoring is enabled
__internal bool monitoring_is_enabled(void);

//! \brief Display monitoring statistics
__internal void monitoring_display_stats(void);


//    TASKS    //

//! \brief Gather basic information about a task when it is created
//! \param[in,out] task The task
__internal void monitoring_task_created(nosv_task_t task);

//! \brief Check whether any action has to be taken when a task is submitted for execution
//! \param[in,out] task The task
__internal void monitoring_task_submitted(nosv_task_t task);

//! \brief Initialize monitoring statistics for a newly created task type
//! \param[in,out] type The type
__internal void monitoring_type_created(nosv_task_type_t type);

//! \brief Change a task statistics after it changes its execution status
//! \param[in,out] task The task changing its status
//! \param[in] status The new execution status of the task
__internal void monitoring_task_changed_status(nosv_task_t task, monitoring_status_t status);

//! \brief Aggregate statistics after a task has finished
//! \param[in,out] task The task that has finished
__internal void monitoring_task_completed(nosv_task_t task);

//! \brief Retreive the size necessary to allocate task statistics
__internal size_t monitoring_get_task_size(void);

//! \brief Retreive the size necessary to allocate tasktype statistics
__internal size_t monitoring_get_tasktype_size(void);


//    CPUS    //

//! \brief Propagate monitoring operations when a CPU becomes idle
//! \param[in] cpu_id The identifier of the CPU
__internal void monitoring_cpu_idle(int cpu_id);

//! \brief Propagate monitoring operations when a CPU becomes active
//! \param[in] cpu_id The identifier of the CPU
__internal void monitoring_cpu_active(int cpu_id);


#endif // MONITORING_H
