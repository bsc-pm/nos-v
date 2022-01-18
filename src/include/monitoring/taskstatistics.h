/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef TASKSTATISTICS_H
#define TASKSTATISTICS_H

#include <stdbool.h>

#include "compiler.h"

#include "monitoringsupport.h"
#include "generic/chrono.h"
#include "hwcounters/hwcounters.h"


typedef struct taskstatistics {
	//! A pointer to the accumulated statistics of this task's tasktype
	void *tasktypestats;
	//! The task's cost
	size_t cost;

	//    TIMING METRICS    //

	//! Array of stopwatches to monitor timing for the task in each status
	chrono_t chronos[num_status];
	//! Id of the currently active stopwatch (status)
	enum monitoring_status_t current_chrono;
	//! The predicted elapsed execution time of the task
	double time_prediction;

	//    HWCOUNTER METRICS    //

	//! Predictions for each hardware counter of the task
	//! NOTE: Predictions of HWCounters must be doubles due to being computed
	//! using normalized values and products
	double *counter_predictions;
} taskstatistics_t;

//! \brief Initialize task statistics
static inline void taskstatistics_init(taskstatistics_t *stats, void *alloc_address)
{
	stats->tasktypestats = NULL;
	stats->cost = DEFAULT_COST;
	stats->current_chrono = null_status;
	stats->time_prediction = PREDICTION_UNAVAILABLE;
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

static inline bool taskstatistics_has_time_prediction(taskstatistics_t *stats)
{
	assert(stats != NULL);
	return (bool) (stats->time_prediction != PREDICTION_UNAVAILABLE);
}

static inline void taskstatistics_set_time_prediction(taskstatistics_t *stats, double prediction)
{
	assert(stats != NULL);
	stats->time_prediction = prediction;
}

static inline double taskstatistics_get_time_prediction(taskstatistics_t *stats)
{
	assert(stats != NULL);
	return stats->time_prediction;
}

static inline bool taskstatistics_has_counter_prediction(taskstatistics_t *stats, size_t counter_id)
{
	assert(stats != NULL);
	return (bool) (stats->counter_predictions[counter_id] != PREDICTION_UNAVAILABLE);
}

static inline void taskstatistics_set_counter_prediction(taskstatistics_t *stats, double prediction, size_t counter_id)
{
	assert(stats != NULL);
	stats->counter_predictions[counter_id] = prediction;
}

static inline double taskstatistics_get_counter_prediction(taskstatistics_t *stats, size_t counter_id)
{
	assert(stats != NULL);
	return stats->counter_predictions[counter_id];
}


//    TIMING-RELATED METHODS    //

//! \brief Start/resume a chrono. If resumed, the active chrono must pause
//! \param[in] id the timing status of the stopwatch to start/resume
//! \return The previous timing status of the task
static inline enum monitoring_status_t taskstatistics_start_timing(
	taskstatistics_t *stats, enum monitoring_status_t id
) {
	assert(stats != NULL);
	assert(id < num_status);

	// Change the current timing status
	const enum monitoring_status_t old_id = stats->current_chrono;
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
static inline enum monitoring_status_t taskstatistics_stop_timing(taskstatistics_t *stats)
{
	assert(stats != NULL);

	const enum monitoring_status_t oldid = stats->current_chrono;
	if (oldid != null_status) {
		chrono_stop(&(stats->chronos[oldid]));
	}
	stats->current_chrono = null_status;

	return oldid;
}

//! \brief Get the elapsed execution time of the task
static inline double taskstatistics_get_elapsed_time(taskstatistics_t *stats)
{
	assert(stats != NULL);

	// Return the aggregation of timing of the task
	return chrono_get_elapsed(&(stats->chronos[executing_status]));
}

//! \brief Retreive the size needed to allocate dynamic structures
static inline size_t taskstatistics_get_allocation_size()
{
	return (hwcounters_get_num_enabled_counters() * sizeof(double));
}

#endif // TASKSTATISTICS_H
