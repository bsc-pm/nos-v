/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef ACCUMULATOR_H
#define ACCUMULATOR_H

#include <assert.h>
#include <math.h>
#include <stddef.h>

#define ROLLING_WINDOW 20


//! \brief Accumulator. Entity that accepts data while maintaining statistics
typedef struct accumulator {
	// List of values (circular array)
	double values[ROLLING_WINDOW];
	// The index of the oldest value in the list of values
	size_t oldest;
	// The moving average of the last ROLLING_WINDOW values
	double moving_avg;
	// The sum of all the values currently in the list
	double sum;
	// Number of elements taken into account for the rolling average
	size_t num;
	// Sum of all the values that have been through / are in the list
	double total_sum;
	// The number of values that have been through the list
	size_t total_num;
} accumulator_t;


//! \brief Initialize an accumulator
static inline void accumulator_init(accumulator_t *acc)
{
	acc->oldest = 0;
	acc->moving_avg = 0.0;
	acc->sum = 0.0;
	acc->num = 0;
	acc->total_sum = 0.0;
	acc->total_num = 0;
}

//! \brief Add a value to the accumulator
static inline void accumulator_add(accumulator_t *acc, double val)
{
	// Update global statitics
	acc->total_sum += val;
	acc->total_num++;

	// Update the number of elements in the rolling average only if needed
	short is_full = (acc->num == ROLLING_WINDOW);
	size_t index_newest = (is_full) ? acc->oldest : acc->num;
	double value_oldest = (is_full) ? acc->values[acc->oldest] : 0.0;

	if (is_full) {
		++acc->oldest;
	}
	acc->values[index_newest] = val;
	acc->num += !(is_full);
	acc->sum += val - value_oldest;
	acc->moving_avg = acc->sum / acc->num;
}

static inline double accumulator_mean(accumulator_t *acc)
{
	return acc->moving_avg;
}

static inline double accumulator_stddev(accumulator_t *acc)
{
	double variance = 0.0;
	double mean = acc->moving_avg;
	for (size_t i = 0; i < acc->num; ++i) {
		variance += (acc->values[i] - mean)*(acc->values[i] - mean);
	}

	return sqrt(variance/acc->num);
}

static inline double accumulator_sum(accumulator_t *acc)
{
	return acc->sum;
}

static inline double accumulator_total_sum(accumulator_t *acc)
{
	return acc->total_sum;
}

static inline size_t accumulator_total_num(accumulator_t *acc)
{
	return acc->total_num;
}

#endif // ACCUMULATOR_H
