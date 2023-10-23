/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPUSET_H
#define CPUSET_H

#include <assert.h>
#include <sched.h>
#include <string.h>

#include "common.h"

// Returns the CPU id of the first bit set in the input cpu_set_t. If not bit is
// set, -1 is returned.
static inline int CPU_FIRST_S(size_t setsize, cpu_set_t *set)
{
	assert(set);
	int cpu = -1;
	for (int i = 0; i < 8 * setsize; i++) {
		if (CPU_ISSET_S(i, setsize, set)) {
			cpu = i;
			break;
		}
	}

	return cpu;
}

// Copy the src_set into the dst_set. If the dst_set is bigger than the src_set,
// fill the remaining bytes with zeroes.
static inline void CPU_COPY_S(
	size_t dst_setsize, cpu_set_t *dst_set,
	size_t src_setsize, cpu_set_t *src_set
) {

	if (dst_setsize <= src_setsize) {
		memcpy(dst_set, src_set, dst_setsize);
	} else {
		memcpy(dst_set, src_set, src_setsize);
		memset(((char *)dst_set) + src_setsize, 0, dst_setsize - src_setsize);
	}
}

#endif // CPUSET_H
