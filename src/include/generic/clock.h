/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
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

static inline uint64_t clock_fast_ns() {
	struct timespec tp;
	clock_gettime(CLK_SRC_FAST, &tp);
	return tp.tv_sec * 1000000000ULL + tp.tv_nsec;
}

static inline uint64_t clock_ns() {
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return tp.tv_sec * 1000000000ULL + tp.tv_nsec;
}

#endif // CLOCK_H
