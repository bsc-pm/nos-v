/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2026 Barcelona Supercomputing Center (BSC)
*/

#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#ifdef CLOCK_MONOTONIC_COARSE
#define CLK_SRC_FAST CLOCK_MONOTONIC_COARSE
#else
#define CLK_SRC_FAST CLOCK_MONOTONIC
#endif

static inline uint64_t clock_fast_ns(void) {
	struct timespec tp;
	clock_gettime(CLK_SRC_FAST, &tp);
	return tp.tv_sec * 1000000000ULL + tp.tv_nsec;
}

static inline uint64_t clock_ns(void) {
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return tp.tv_sec * 1000000000ULL + tp.tv_nsec;
}

static inline int clock_ns_from_id(clockid_t clock_id, uint64_t *time)
{
	struct timespec tp;
	if (clock_gettime(clock_id, &tp) != 0 || time == NULL) {
		return -1;
	}

	*time = tp.tv_sec * 1000000000ULL + tp.tv_nsec;
	return 0;
}

#endif // CLOCK_H
