/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef TASKTYPESTATISTICS_H
#define TASKTYPESTATISTICS_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

#include "compiler.h"
#include "nosv.h"

#include "monitoringsupport.h"
#include "taskstatistics.h"
#include "generic/accumulator.h"
#include "hwcounters/hwcounters.h"
#include "hwcounters/taskhwcounters.h"


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

	//    HARDWARE COUNTER METRICS    //

	//! An array of hardware counter accumulators
	accumulator_t *counter_accumulators;
	//! An array of hardware counter accumulators with normalized metrics
	accumulator_t *normalized_counter_accumulators;
	//! An array of hardware counter accumulators for the accuracy of predictions
	accumulator_t *counter_accuracy_accumulators;
	//! Spinlock to ensure atomic access within the previous accumulators
	nosv_spinlock_t counters_lock;
} tasktypestatistics_t;


static inline void tasktypestatistics_init(tasktypestatistics_t *stats)
{
	assert(stats != NULL);

	stats->accumulated_cost = 0;
	stats->num_predictionless_instances = 0;

	accumulator_init(&(stats->timing_accumulator));
	accumulator_init(&(stats->timing_accuracy_accumulator));

	const size_t num_enabled_counters = hwcounters_get_num_enabled_counters();

	void *inner_alloc_address = (char *) stats + sizeof(tasktypestatistics_t);
	stats->counter_accumulators = (accumulator_t *) inner_alloc_address;
	stats->normalized_counter_accumulators = (accumulator_t *) ((char *) stats->counter_accumulators + (sizeof(accumulator_t) * num_enabled_counters));
	stats->counter_accuracy_accumulators = (accumulator_t *) ((char *) stats->normalized_counter_accumulators + (sizeof(accumulator_t) * num_enabled_counters));

	for (size_t i = 0; i < num_enabled_counters; ++i) {
		accumulator_init(&(stats->counter_accumulators[i]));
		accumulator_init(&(stats->normalized_counter_accumulators[i]));
		accumulator_init(&(stats->counter_accuracy_accumulators[i]));
	}

	nosv_spin_init(&(stats->lock));
	nosv_spin_init(&(stats->counters_lock));
}

static inline size_t tasktypestatistics_get_allocation_size()
{
	return (hwcounters_get_num_enabled_counters() * 3 * sizeof(accumulator_t));
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


//    HARDWARE COUNTER PREDICTIONS    //

//! \brief Insert a metric into the corresponding accumulator
static inline void tasktypestatistics_insert_normalized_counter(
	tasktypestatistics_t *type_stats, double value, size_t counter_id
) {
	assert(type_stats != NULL);

	// Try to inferr a prediction
	nosv_spin_lock(&(type_stats->counters_lock));
	accumulator_add(&(type_stats->normalized_counter_accumulators[counter_id]), value);
	nosv_spin_unlock(&(type_stats->counters_lock));
}

//! \brief Retreive, for a certain type of counter, the sum of accumulated
//! values of all tasks from this type
static inline double tasktypestatistics_get_counter_sum(
	tasktypestatistics_t *type_stats, size_t counter_id
) {
	nosv_spin_lock(&(type_stats->counters_lock));
	double sum = accumulator_total_sum(&(type_stats->counter_accumulators[counter_id]));
	nosv_spin_unlock(&(type_stats->counters_lock));

	return sum;
}

//! \brief Retreive, for a certain type of counter, the average of all
//! accumulated values of this task type
static inline double tasktypestatistics_get_counter_average(
	tasktypestatistics_t *type_stats, size_t counter_id
) {
	nosv_spin_lock(&(type_stats->counters_lock));
	size_t count = accumulator_total_num(&(type_stats->counter_accumulators[counter_id]));
	double sum = accumulator_total_sum(&(type_stats->counter_accumulators[counter_id]));
	nosv_spin_unlock(&(type_stats->counters_lock));

	return (sum / count);
}

//! \brief Retreive, for a certain type of counter, the standard deviation
//! taking into account all the values in the accumulator of this task type
static inline double tasktypestatistics_get_counter_stddev(
	tasktypestatistics_t *type_stats, size_t counter_id
) {
	nosv_spin_lock(&(type_stats->counters_lock));
	double stddev = accumulator_stddev(&(type_stats->counter_accumulators[counter_id]));
	nosv_spin_unlock(&(type_stats->counters_lock));

	return stddev;
}

//! \brief Retreive, for a certain type of counter, the amount of values
//! in the accumulator (i.e., the number of tasks)
static inline size_t tasktypestatistics_get_counter_num_instances(
	tasktypestatistics_t *type_stats, size_t counter_id
) {
	nosv_spin_lock(&(type_stats->counters_lock));
	size_t count = accumulator_total_num(&(type_stats->counter_accumulators[counter_id]));
	nosv_spin_unlock(&(type_stats->counters_lock));

	return count;
}

//! \brief Retreive, for a certain type of counter, the average of all
//! accumulated normalized values of this task type
static inline double tasktypestatistics_get_normalized_counter_rolling_average(
	tasktypestatistics_t *type_stats, size_t counter_id
) {
	nosv_spin_lock(&(type_stats->counters_lock));
	double avg = accumulator_mean(&(type_stats->normalized_counter_accumulators[counter_id]));
	nosv_spin_unlock(&(type_stats->counters_lock));

	return avg;
}

//! \brief Retreive, for a certain type of counter, the average accuracy
//! of counter predictions of this tasktype
static inline double tasktypestatistics_get_counter_accuracy(
	tasktypestatistics_t *type_stats, size_t counter_id
) {
	nosv_spin_lock(&(type_stats->counters_lock));
	double avg = accumulator_mean(&(type_stats->counter_accuracy_accumulators[counter_id]));
	nosv_spin_unlock(&(type_stats->counters_lock));

	return avg;
}

static inline double tasktypestatistics_get_counter_prediction(
	tasktypestatistics_t *type_stats, size_t cost, size_t counter_id
) {
	double normalized_value = PREDICTION_UNAVAILABLE;

	// Check if a prediction can be inferred
	nosv_spin_lock(&(type_stats->counters_lock));
	if (accumulator_total_num(&(type_stats->normalized_counter_accumulators[counter_id]))) {
		normalized_value = ((double) cost) * accumulator_mean(&(type_stats->normalized_counter_accumulators[counter_id]));
	}
	nosv_spin_unlock(&(type_stats->counters_lock));

	return normalized_value;
}


//    SHARED FUNCTIONS: MONITORING + HWC    //

//! \brief Accumulate a task's timing statisics and counters to inferr
//! predictions. More specifically, this function:
//! - Normalizes task metrics with its cost to later insert these normalized
//!   metrics into accumulators
//! - If there were predictions for the previous metrics, the accuracy is
//!   computed and inserted into accuracy accumulators
static inline void tasktypestatistics_accumulate_stats_and_counters(
	tasktypestatistics_t *type_stats,
	taskstatistics_t *task_stats,
	task_hwcounters_t *task_counters
) {
	assert(type_stats != NULL);
	assert(task_stats != NULL);
	assert(task_counters != NULL);

	double cost = (double) task_stats->cost;

	//    TIMING    //

	// Normalize the execution time using the task's computational cost
	double elapsed = taskstatistics_get_elapsed_time(task_stats);
	double normalized_time = elapsed / cost;

	// Compute the accuracy of the prediction if the task had one
	double accuracy = 0.0;
	bool prediction_available = (taskstatistics_has_time_prediction(task_stats) && (elapsed > 0.0));
	if (prediction_available) {
		double predicted = taskstatistics_get_time_prediction(task_stats);
		double max_value = (elapsed > predicted) ? elapsed : predicted;
		double error = 100.0 * (fabs(predicted - elapsed) / max_value);
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

	//    HARDWARE COUNTERS    //

	const enum counters_t *enabled_counters = hwcounters_get_enabled_counters();
	const size_t num_enabled_counters = hwcounters_get_num_enabled_counters();

	// Pre-compute all the needed values before entering the lock
	double counters[num_enabled_counters];
	double normalized_counters[num_enabled_counters];
	bool counter_predictions_available[num_enabled_counters];
	double counter_accuracies[num_enabled_counters];
	for (size_t id = 0; id < num_enabled_counters; ++id) {
		counters[id] = (double) taskhwcounters_get_accumulated(task_counters, enabled_counters[id]);
		normalized_counters[id] = (counters[id] / cost);
		counter_predictions_available[id] =
			(taskstatistics_has_counter_prediction(task_stats, id) && counters[id] > 0.0);

		if (counter_predictions_available[id]) {
			double predicted = taskstatistics_get_counter_prediction(task_stats, id);
			double max_value = (counters[id] > predicted) ? counters[id] : predicted;
			double error = 100.0 * (fabs(predicted - counters[id]) / max_value);
			counter_accuracies[id] = 100.0 - error;
		}
	}

	// Aggregate all the information into the accumulators
	nosv_spin_lock(&(type_stats->counters_lock));
	for (size_t id = 0; id < num_enabled_counters; ++id) {
		accumulator_add(&(type_stats->counter_accumulators[id]), counters[id]);
		accumulator_add(&(type_stats->normalized_counter_accumulators[id]), normalized_counters[id]);

		if (counter_predictions_available[id]) {
			accumulator_add(&(type_stats->counter_accuracy_accumulators[id]), counter_accuracies[id]);
		}
	}
	nosv_spin_unlock(&(type_stats->counters_lock));
}

#endif // TASKTYPESTATISTICS_H
