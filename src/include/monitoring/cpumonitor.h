/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPUMONITOR_H
#define CPUMONITOR_H

#include <stddef.h>

#include "compiler.h"
#include "cpustats.h"
#include "memory/slab.h"


typedef struct cpumonitor {
	cpu_stats_t *cpu_stats;
	size_t num_cpus;
} cpumonitor_t;

//! \brief Initialize the CPU monitor
//! \param[in,out] monitor The CPU monitor
__internal void cpumonitor_initialize(cpumonitor_t *monitor);

//! \brief Shutdown the CPU monitor
//! \param[in,out] monitor The CPU monitor
__internal void cpumonitor_shutdown(cpumonitor_t *monitor);

//! \brief Signal that a CPU just became active
//! \param[in,out] monitor The CPU monitor
//! \param[in] cpu_id The identifier of the CPU
__internal void cpumonitor_cpu_active(cpumonitor_t *monitor, int cpu_id);

//! \brief Signal that a CPU just became idle
//! \param[in,out] monitor The CPU monitor
//! \param[in] cpu_id The identifier of the CPU
__internal void cpumonitor_cpu_idle(cpumonitor_t *monitor, int cpu_id);

//! \brief Retreive the activeness of a CPU
//! \param[in,out] monitor The CPU monitor
//! \param[in] cpu_id The identifier of the CPU
__internal double cpumonitor_get_activeness(cpumonitor_t *monitor, int cpu_id);

//! \brief Get the total amount of activeness of all CPUs
//! \param[in,out] monitor The CPU monitor
__internal double cpumonitor_get_total_activeness(cpumonitor_t *monitor);

//! \brief Return the number of CPUs in the system
//! \param[in,out] monitor The CPU monitor
__internal size_t cpumonitor_get_num_cpus(cpumonitor_t *monitor);

//! \brief Display CPU statistics
//! \param[in,out] monitor The CPU monitor
__internal void cpumonitor_statistics(cpumonitor_t *monitor);

#endif // CPUMONITOR_H
