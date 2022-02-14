/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef CHRONO_H
#define CHRONO_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>


//! \brief A basic chronometer
typedef struct chrono {
	// The accumulated elapsed execution time
	double elapsed;
	// Start time checkpoint
	struct timespec begin;
} chrono_t;

static inline void chrono_init(chrono_t *timer)
{
	assert(timer != NULL);

	timer->elapsed = 0.0;
}

static inline void chrono_start(chrono_t *timer)
{
	assert(timer != NULL);

	clock_gettime(CLOCK_MONOTONIC, &(timer->begin));
}

static inline void chrono_stop(chrono_t *timer)
{
	assert(timer != NULL);

	struct timespec end;
	clock_gettime(CLOCK_MONOTONIC, &end);

	uint64_t end_time = (end.tv_sec * 1e9) + end.tv_nsec;
	uint64_t begin_time = (timer->begin.tv_sec * 1e9) + timer->begin.tv_nsec;
	assert(end_time >= begin_time);

	timer->elapsed += (double) ((end_time - begin_time) / (double) 1e9);
}

static inline void chrono_continue_at(chrono_t *timer, chrono_t *other)
{
	chrono_stop(timer);
	chrono_start(other);
}

//! \brief Get the elapsed time of a timer in seconds
static inline double chrono_get_elapsed(chrono_t *timer)
{
	assert(timer != NULL);

	return timer->elapsed;
}

#endif // CHRONO_H
