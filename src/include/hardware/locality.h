/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef LOCALITY_H
#define LOCALITY_H

#include "climits.h"
#include "compiler.h"

__internal void locality_init();
__internal int locality_numa_count();
__internal int locality_get_cpu_numa(int system_cpu_id);
__internal int locality_get_logical_numa(int system_numa_id);
__internal void locality_shutdown();

#endif // LOCALITY_H