/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef MONITORING_H
#define MONITORING_H

#include "compiler.h"
#include "cpumonitor.h"
#include "monitoringsupport.h"
#include "nosv.h"
//TODO #include "taskmonitor.h"


typedef struct monitoring_manager {
	//! The lock of the manager
	nosv_spinlock_t lock;

	//! Whether verbosity for monitoring enabled
	short verbose;

	//! A monitor that handles CPU statistics
	cpumonitor_t *cpumonitor;

	//! A monitor that handles task statistics
	//TODO taskmonitor_t *taskmonitor;
} monitoring_manager_t;

//! \brief Initialize monitoring structures
//! \param[in] initialize Whether this is the first time (process) initializing monitoring
__internal void monitoring_init(short initialize);

//! \brief Shutdown monitoring
__internal void monitoring_shutdown();

//! \brief Check whether monitoring is enabled
__internal short monitoring_is_enabled();

//! \brief Display monitoring statistics
__internal void monitoring_display_stats();


//    TASKS    //

//! \brief Gather basic information about a task when it is created
//! \param[in,out] task The task
__internal void monitoring_task_created(nosv_task_t task);

// NOTE: Taskfor not yet supported
////! \brief Reset monitoring statistics for a task that will be re-used
////! \param[in,out] task The task
//__internal void monitoring_task_reinitialized(nosv_task_t task);

//! \brief Change a task statistics after it changes its execution status
//! \param[in,out] task The task changing its status
//! \param[in] status The new execution status of the task
__internal void monitoring_task_changed_status(nosv_task_t task, enum monitoring_status_t status);

//! \brief Monitoring when a task completes user code execution
//! \param[in,out] task The task that has completed the execution
__internal void monitoring_task_completed(nosv_task_t task);

//! \brief Aggregate statistics after a task has finished
//! \param[in,out] task The task that has finished
__internal void monitoring_task_finished(nosv_task_t task);


//    CPUS    //

//! \brief Propagate monitoring operations when a CPU becomes idle
//! \param[in] cpu_id The identifier of the CPU
__internal void monitoring_cpu_idle(int cpu_id);

//! \brief Propagate monitoring operations when a CPU becomes active
//! \param[in] cpu_id The identifier of the CPU
__internal void monitoring_cpu_active(int cpu_id);


#endif // MONITORING_H
