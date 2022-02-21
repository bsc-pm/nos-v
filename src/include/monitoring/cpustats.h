/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPUSTATS_H
#define CPUSTATS_H

#include "compiler.h"
#include "generic/chrono.h"


enum cpu_status_t {
	idle_status = 0,
	active_status,
	num_cpu_status
};

typedef struct cpu_stats {
	enum cpu_status_t current_status;
	chrono_t chronos[num_cpu_status];
} cpu_stats_t;

__internal void cpu_stats_init(cpu_stats_t *stats);
__internal void cpu_stats_active(cpu_stats_t *stats);
__internal void cpu_stats_idle(cpu_stats_t *stats);
__internal double cpu_stats_get_activeness(cpu_stats_t *stats);

#endif // CPUSTATS_H
