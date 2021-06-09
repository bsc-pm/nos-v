/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef LOCALITY_H
#define LOCALITY_H

#include "climits.h"

void locality_init();
int locality_numa_count();
int locality_get_cpu_numa(int system_cpu_id);
int locality_get_logical_numa(int system_numa_id);
void locality_shutdown();

#endif // LOCALITY_H