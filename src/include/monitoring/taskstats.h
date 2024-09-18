/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef TASKSTATS_H
#define TASKSTATS_H

#include <stdbool.h>

#include "compiler.h"
#include "monitoringsupport.h"
#include "generic/chrono.h"
#include "hwcounters/hwcounters.h"


typedef struct task_stats {
	//! A pointer to the accumulated statistics of this task's tasktype
	void *tasktypestats;
	//! The task's cost
	uint64_t cost;
	//! Whether the prediction-related fields have already been initialized
	bool initialized;

	//    TIMING METRICS    //

	//! Array of stopwatches to monitor timing for the task in each status
	chrono_t chronos[num_status];
	//! Id of the currently active stopwatch (status)
	monitoring_status_t current_chrono;
	//! The predicted elapsed execution time of the task
	double time_prediction;

	//    HWCOUNTER METRICS    //

	//! Predictions for each hardware counter of the task
	//! NOTE: Predictions of HWCounters must be doubles due to being computed
	//! using normalized values and products
	double *counter_predictions;
} task_stats_t;

//! \brief Initialize task statistics
static inline void task_stats_init(task_stats_t *stats, void *alloc_address)
{
	stats->tasktypestats = NULL;
	stats->cost = DEFAULT_COST;
	stats->current_chrono = null_status;
	stats->time_prediction = PREDICTION_UNAVAILABLE;
	stats->initialized = false;
	for (size_t i = 0; i < num_status; ++i) {
		chrono_init(&(stats->chronos[i]));
	}

	const size_t num_counters = hwcounters_get_num_enabled_counters();
	assert(num_counters == 0 || alloc_address != NULL);

	stats->counter_predictions = (double *) alloc_address;
	for (size_t i = 0; i < num_counters; ++i) {
		stats->counter_predictions[i] = PREDICTION_UNAVAILABLE;
	}
}

static inline bool task_stats_has_time_prediction(task_stats_t *stats)
{
	assert(stats != NULL);
	return (stats->time_prediction != PREDICTION_UNAVAILABLE);
}

static inline void task_stats_set_time_prediction(task_stats_t *stats, double prediction)
{
	assert(stats != NULL);
	stats->time_prediction = prediction;
}

static inline double task_stats_get_time_prediction(task_stats_t *stats)
{
	assert(stats != NULL);
	return stats->time_prediction;
}

static inline bool task_stats_has_counter_prediction(task_stats_t *stats, size_t counter_id)
{
	assert(stats != NULL);
	return (stats->counter_predictions[counter_id] != PREDICTION_UNAVAILABLE);
}

static inline void task_stats_set_counter_prediction(task_stats_t *stats, size_t counter_id, double prediction)
{
	assert(stats != NULL);
	stats->counter_predictions[counter_id] = prediction;
}

static inline double task_stats_get_counter_prediction(task_stats_t *stats, size_t counter_id)
{
	assert(stats != NULL);
	return stats->counter_predictions[counter_id];
}


//    TIMING-RELATED METHODS    //

//! \brief Start/resume a chrono. If resumed, the active chrono must pause
//! \param[in] id the timing status of the stopwatch to start/resume
//! \return The previous timing status of the task
static inline monitoring_status_t task_stats_start_timing(
	task_stats_t *stats, monitoring_status_t id
) {
	assert(stats != NULL);
	assert(id < num_status);

	// Change the current timing status
	const monitoring_status_t old_id = stats->current_chrono;
	stats->current_chrono = id;

	// Resume the next chrono
	if (old_id == null_status) {
		chrono_start(&(stats->chronos[id]));
	} else {
		chrono_continue_at(&(stats->chronos[old_id]), &(stats->chronos[id]));
	}

	return old_id;
}

//! \brief Stop/pause a chrono
//! \return The previous timing status of the task
static inline monitoring_status_t task_stats_stop_timing(task_stats_t *stats)
{
	assert(stats != NULL);

	const monitoring_status_t oldid = stats->current_chrono;
	if (oldid != null_status) {
		chrono_stop(&(stats->chronos[oldid]));
	}
	stats->current_chrono = null_status;

	return oldid;
}

//! \brief Get the elapsed execution time of the task
static inline double task_stats_get_elapsed_time(task_stats_t *stats)
{
	assert(stats != NULL);

	// Return the aggregation of timing of the task
	return chrono_get_elapsed(&(stats->chronos[executing_status]));
}

//! \brief Retrieve the size needed to allocate dynamic structures
static inline size_t task_stats_get_allocation_size(void)
{
	return (hwcounters_get_num_enabled_counters() * sizeof(double));
}

#endif // TASKSTATS_H
