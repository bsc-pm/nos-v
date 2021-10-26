/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef UTILS_H
#define UTILS_H

#include <sched.h>

static inline int get_num_available_cpus(void)
{
	cpu_set_t set;
	sched_getaffinity(0, sizeof(set), &set);

	return CPU_COUNT(&set);
}

static inline int get_first_available_cpu(void)
{
	cpu_set_t set;
	sched_getaffinity(0, sizeof(set), &set);

	for (int c = 0; c < CPU_SETSIZE; ++c) {
		if (CPU_ISSET(c, &set)) {
			return c;
		}
	}

	return -1;
}

#endif // UTILS_H
