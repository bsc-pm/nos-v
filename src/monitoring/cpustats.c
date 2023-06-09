/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include "monitoring/cpustats.h"


void cpu_stats_init(cpu_stats_t *cpu_stats)
{
	assert(cpu_stats != NULL);

	// Start this CPU as currently idle
	cpu_stats->current_status = idle_status;
	chrono_start(&(cpu_stats->chronos[idle_status]));
}

void cpu_stats_active(cpu_stats_t *cpu_stats)
{
	assert(cpu_stats != NULL);
	assert(cpu_stats->current_status != active_status);

	chrono_stop(&(cpu_stats->chronos[cpu_stats->current_status]));
	cpu_stats->current_status = active_status;
	chrono_start(&(cpu_stats->chronos[cpu_stats->current_status]));
}

void cpu_stats_idle(cpu_stats_t *cpu_stats)
{
	assert(cpu_stats != NULL);
	assert(cpu_stats->current_status != idle_status);

	chrono_stop(&(cpu_stats->chronos[cpu_stats->current_status]));
	cpu_stats->current_status = idle_status;
	chrono_start(&(cpu_stats->chronos[cpu_stats->current_status]));
}

double cpu_stats_get_activeness(cpu_stats_t *cpu_stats)
{
	assert(cpu_stats != NULL);

	chrono_t *chrono = &(cpu_stats->chronos[cpu_stats->current_status]);
	assert(chrono != NULL);

	// Start & stop the current chrono to update the accumulated values
	chrono_stop(chrono);
	chrono_start(chrono);

	double idle   = chrono_get_elapsed(&(cpu_stats->chronos[idle_status]));
	double active = chrono_get_elapsed(&(cpu_stats->chronos[active_status]));

	return (active / (active + idle));
}
