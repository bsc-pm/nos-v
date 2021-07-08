/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef TASKTYPESTATISTICS_H
#define TASKTYPESTATISTICS_H

#include <stdatomic.h>
#include <stdlib.h>

#include "compiler.h"
#include "nosv.h"

#include "monitoringsupport.h"
#include "taskstatistics.h"
#include "generic/accumulator.h"


typedef struct tasktypestatistics {
	//    TIMING METRICS    //

	//! Contains the aggregated computational cost ready to be executed of a tasktype
	atomic_size_t accumulated_cost;
	//! The number of currently active task instances of this type that do not have a prediction
	atomic_size_t num_predictionless_instances;
	//! An accumulator which holds normalized timing measures of tasks
	accumulator_t timing_accumulator;
	//! An accumulator which holds accuracy data of timing predictions
	accumulator_t timing_accuracy_accumulator;
	//! Spinlock to ensure atomic access within the previous accumulators
	nosv_spinlock_t lock;

	//TODO: Missing HWC
} tasktypestatistics_t;


static inline void tasktypestatistics_init(tasktypestatistics_t *stats)
{
	assert(stats != NULL);

	stats->accumulated_cost = 0;
	stats->num_predictionless_instances = 0;

	accumulator_init(&(stats->timing_accumulator));
	accumulator_init(&(stats->timing_accuracy_accumulator));

	nosv_spin_init(&(stats->lock));
}

static inline void tasktypestatistics_increase_accumulated_cost(
	tasktypestatistics_t *stats, size_t cost
) {
	assert(stats != NULL);
	atomic_fetch_add_explicit(&(stats->accumulated_cost), cost, memory_order_relaxed);
}

static inline void tasktypestatistics_decrease_accumulated_cost(
	tasktypestatistics_t *stats, size_t cost
) {
	assert(stats != NULL);

	__maybe_unused size_t previous = atomic_fetch_sub_explicit(
		&(stats->accumulated_cost), cost, memory_order_relaxed
	);
	assert(previous >= cost);
}

static inline size_t tasktypestatistics_get_accumulated_cost(tasktypestatistics_t *stats)
{
	assert(stats != NULL);
	return atomic_load_explicit(&(stats->accumulated_cost), memory_order_relaxed);
}

static inline void tasktypestatistics_increase_num_predictionless_instances(tasktypestatistics_t *stats)
{
	assert(stats != NULL);
	atomic_fetch_add_explicit(&(stats->num_predictionless_instances), 1, memory_order_relaxed);
}

static inline void tasktypestatistics_decrease_num_predictionless_instances(tasktypestatistics_t *stats)
{
	assert(stats != NULL);
	atomic_fetch_sub_explicit(&(stats->num_predictionless_instances), 1, memory_order_relaxed);
}

static inline size_t tasktypestatistics_get_num_predictionless_instances(tasktypestatistics_t *stats)
{
	assert(stats != NULL);
	return atomic_load_explicit(&(stats->num_predictionless_instances), memory_order_relaxed);
}


//    TIMING PREDICTIONS    //

//! \brief Get the rolling average normalized unitary cost of this tasktype
static inline double tasktypestatistics_get_timing_mean(tasktypestatistics_t *stats)
{
	assert(stats != NULL);

	nosv_spin_lock(&(stats->lock));
	double mean = accumulator_mean(&(stats->timing_accumulator));
	nosv_spin_unlock(&(stats->lock));

	return mean;
}

//! \brief Get the standard deviation of the normalized unitary cost of this tasktype
static inline double tasktypestatistics_get_timing_stddev(tasktypestatistics_t *stats)
{
	assert(stats != NULL);

	nosv_spin_lock(&(stats->lock));
	double stddev = accumulator_stddev(&(stats->timing_accumulator));
	nosv_spin_unlock(&(stats->lock));

	return stddev;
}


//! \brief Get the number of task instances that accumulated metrics
static inline size_t tasktypestatistics_get_num_instances(tasktypestatistics_t *stats)
{
	assert(stats != NULL);

	nosv_spin_lock(&(stats->lock));
	size_t instances = accumulator_total_num(&(stats->timing_accumulator));
	nosv_spin_unlock(&(stats->lock));

	return instances;
}

//! \brief Get the average accuracy of timing predictions of this tasktype
static inline double tasktypestatistics_get_timing_accuracy(tasktypestatistics_t *stats)
{
	assert(stats != NULL);

	nosv_spin_lock(&(stats->lock));
	double mean = accumulator_mean(&(stats->timing_accuracy_accumulator));
	nosv_spin_unlock(&(stats->lock));

	return mean;
}

//! \brief Get a timing prediction for a task
//! \param[in] cost The task's computational costs
static inline double tasktypestatistics_get_timing_prediction(tasktypestatistics_t *stats, size_t cost)
{
	assert(stats != NULL);

	double prediction = PREDICTION_UNAVAILABLE;

	// Try to inferr a prediction
	nosv_spin_lock(&(stats->lock));
	if (accumulator_total_num(&(stats->timing_accumulator))) {
		prediction = ((double) cost * accumulator_mean(&(stats->timing_accumulator)));
	}
	nosv_spin_unlock(&(stats->lock));

	return prediction;
}

//! \brief Accumulate a task's timing statisics to inferr predictions. More
//! specifically, this function:
//! - Normalizes task metrics with its cost to later insert these normalized
//!   metrics into accumulators
//! - If there were predictions for the previous metrics, the accuracy is
//!   computed and inserted into accuracy accumulators
//! \param[in] task_stats The task's statistics
//! \param[in] type_stats The task's type statistics
static inline void tasktypestatistics_accumulate(
	tasktypestatistics_t *type_stats,
	taskstatistics_t *task_stats
) {
	assert(type_stats != NULL);
	assert(task_stats != NULL);

	double cost = (double) task_stats->cost;

	//    TIMING    //

	// Normalize the execution time using the task's computational cost
	double elapsed = taskstatistics_get_elapsed_time(task_stats);
	double normalized_time = elapsed / cost;

	// Compute the accuracy of the prediction if the task had one
	double accuracy = 0.0;
	short prediction_available = (taskstatistics_has_time_prediction(task_stats) && (elapsed > 0.0));
	if (prediction_available) {
		double predicted = taskstatistics_get_time_prediction(task_stats);
		double max_value = (elapsed > predicted) ? elapsed : predicted;
		double error = 100.0 * (abs(predicted - elapsed) / max_value);
		accuracy = 100.0 - error;
	}

	// Accumulate the unitary time, the elapsed time to compute effective
	// parallelism metrics, and the accuracy obtained of a previous prediction
	nosv_spin_lock(&(type_stats->lock));
	accumulator_add(&(type_stats->timing_accumulator), normalized_time);
	if (prediction_available) {
		accumulator_add(&(type_stats->timing_accuracy_accumulator), accuracy);
	}
	nosv_spin_unlock(&(type_stats->lock));
}

#endif // TASKTYPESTATISTICS_H
