/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#ifndef UTILS_H
#define UTILS_H

#include <assert.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(f...)                                                                \
do {                                                                               \
	const int __r = f;                                                             \
	if (__r) {                                                                     \
		fprintf(stderr, "Error: '%s' [%s:%i]: %i\n", #f, __FILE__, __LINE__, __r); \
		exit(EXIT_FAILURE);                                                        \
	}                                                                              \
} while (0)

static inline int get_cpus(void)
{
	cpu_set_t set;
	sched_getaffinity(0, sizeof(set), &set);
	return CPU_COUNT(&set);
}

static inline int *get_cpu_array(void)
{
	cpu_set_t set;
	sched_getaffinity(0, sizeof(set), &set);
	int cnt = CPU_COUNT(&set);
	int *array = malloc(cnt * sizeof(int));
	assert(array);

	int i = 0;
	int seen = 0;

	while (seen < cnt) {
		if (CPU_ISSET(i, &set))
			array[seen++] = i;
		++i;
	}

	assert(seen == cnt);

	return array;
}

static inline int get_first_cpu(void)
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
