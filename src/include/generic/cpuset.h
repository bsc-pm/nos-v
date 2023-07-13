/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPUSET_H
#define CPUSET_H

#include <assert.h>
#include <sched.h>


// Returns the CPU id of the first bit set in the input cpu_set_t. If not bit is
// set, -1 is returned.
static inline int CPU_FIRST(cpu_set_t *set)
{
	assert(set);
	int cpu = -1;
	for (int i = 0; i < 8 * sizeof(*set); i++) {
		if (CPU_ISSET(i, set)) {
			cpu = i;
			break;
		}
	}

	return cpu;
}

#endif // CPUSET_H
