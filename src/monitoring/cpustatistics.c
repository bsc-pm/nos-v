/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include "monitoring/cpustatistics.h"


void cpustatistics_init(cpustatistics_t *cpu_stats)
{
	// Start this CPU as currently idle
	cpu_stats->current_status = idle_status;
	chrono_start(&(cpu_stats->chronos[idle_status]));
}

void cpustatistics_active(cpustatistics_t *cpu_stats)
{
	chrono_stop(&(cpu_stats->chronos[cpu_stats->current_status]));
	cpu_stats->current_status = active_status;
	chrono_start(&(cpu_stats->chronos[cpu_stats->current_status]));
}

void cpustatistics_idle(cpustatistics_t *cpu_stats)
{
	chrono_stop(&(cpu_stats->chronos[cpu_stats->current_status]));
	cpu_stats->current_status = idle_status;
	chrono_start(&(cpu_stats->chronos[cpu_stats->current_status]));
}

float cpustatistics_get_activeness(cpustatistics_t *cpu_stats)
{
	chrono_t *chrono = &(cpu_stats->chronos[cpu_stats->current_status]);
	assert(chrono != NULL);

	// Start & stop the current chrono to update the accumulated values
	chrono_stop(chrono);
	chrono_start(chrono);

	double idle   = chrono_get_elapsed(&(cpu_stats->chronos[idle_status]));
	double active = chrono_get_elapsed(&(cpu_stats->chronos[active_status]));

	return (active / (active + idle));
}
