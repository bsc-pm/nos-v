/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPUSTATISTICS_H
#define CPUSTATISTICS_H

#include "compiler.h"
#include "generic/chrono.h"


enum cpu_status_t {
	idle_status = 0,
	active_status,
	num_cpu_status
};

typedef struct cpustatistics {
	enum cpu_status_t current_status;
	chrono_t chronos[num_cpu_status];
} cpustatistics_t;

__internal void cpustatistics_init(cpustatistics_t *cpu_stats);
__internal void cpustatistics_active(cpustatistics_t *cpu_stats);
__internal void cpustatistics_idle(cpustatistics_t *cpu_stats);
__internal float cpustatistics_get_activeness(cpustatistics_t *cpu_stats);

#endif // CPUSTATISTICS_H
