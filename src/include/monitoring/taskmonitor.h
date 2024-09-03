/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef TASKMONITOR_H
#define TASKMONITOR_H

#include <math.h>
#include <stdio.h>

#include "nosv-internal.h"
#include "taskstats.h"
#include "tasktypestats.h"
#include "hwcounters/hwcounters.h"
#include "hwcounters/supportedhwcounters.h"
#include "hwcounters/taskhwcounters.h"
#include "system/tasks.h"


//! \brief Initialize a task's monitoring statistics
//! \param[in,out] task The task
static inline void taskmonitor_task_created(nosv_task_t task)
{
	assert(task != NULL);
	assert(task->type != NULL);

	task_stats_t *task_stats = task->stats;
	assert(task_stats != NULL);

	void *inner_alloc_address = (char *) task_stats + sizeof(task_stats_t);

	// Initialize attributes of the new task
	task_stats_init(task_stats, inner_alloc_address);

	// Set the task's tasktype statistics for future references
	task_stats->tasktypestats = task->type->stats;
}

//! \brief Check whether any action must be taken when a task is submitted
//! \param[in,out] task The task
static inline void taskmonitor_task_submitted(nosv_task_t task)
{
	assert(task != NULL);
	assert(task->type != NULL);

	task_stats_t *task_stats = task->stats;
	assert(task_stats != NULL);

	// Check whether predictions have been inferred yet
	if (!task_stats->initialized) {
		task_stats->initialized = true;

		// Predict metrics using past data
		tasktype_stats_t *type_stats = task_stats->tasktypestats;

		// Retreive the cost of the task
		uint64_t cost = DEFAULT_COST;
		if (task->type->get_cost != NULL) {
			cost = task->type->get_cost(task);
		}
		task_stats->cost = cost;

		// Predict timing metrics
		double time_prediction = tasktype_stats_get_timing_prediction(type_stats, cost);
		if (time_prediction != PREDICTION_UNAVAILABLE) {
			task_stats_set_time_prediction(task_stats, time_prediction);
		}

		// Predict hwcounter metrics
		size_t num_counters = hwcounters_get_num_enabled_counters();
		for (size_t i = 0; i < num_counters; ++i) {
			double counter_prediction = tasktype_stats_get_counter_prediction(type_stats, cost, i);
			if (counter_prediction != PREDICTION_UNAVAILABLE) {
				task_stats_set_counter_prediction(task_stats, i, counter_prediction);
			}
		}
	}
}

static inline void taskmonitor_task_started(nosv_task_t task, monitoring_status_t status)
{
	assert(task != NULL);

	task_stats_t *task_stats = task->stats;
	assert(task_stats != NULL);

	// Start recording time for the new execution status
	monitoring_status_t old_status = task_stats_start_timing(task_stats, status);

	// If this is the first time the task becomes ready, increase the cost
	// accumulations used to infer predictions
	if (old_status == null_status && status == ready_status) {
		tasktype_stats_t *type_stats = task_stats->tasktypestats;
		if (task_stats_has_time_prediction(task_stats)) {
			tasktype_stats_increase_accumulated_cost(type_stats, task_stats->cost);
		} else {
			tasktype_stats_increase_num_predictionless_instances(type_stats);
		}
	}
}

static inline void taskmonitor_task_completed(nosv_task_t task)
{
	// NOTE: No need for task completed user code, as tasks don't wait for their children
	assert(task != NULL);

	task_stats_t *task_stats = task->stats;
	task_hwcounters_t *task_counters = task->counters;
	assert(task_stats != NULL);

	// Stop timing for the task
	task_stats_stop_timing(task_stats);

	// Accumulate timing statistics and counters into the task's type
	tasktype_stats_t *type_stats = task_stats->tasktypestats;
	tasktype_stats_accumulate_stats_and_counters(type_stats, task_stats, task_counters);

	if (task_stats_has_time_prediction(task_stats)) {
		tasktype_stats_decrease_accumulated_cost(type_stats, task_stats->cost);
	} else {
		tasktype_stats_decrease_num_predictionless_instances(type_stats);
	}
}

static inline void taskmonitor_statistics(void)
{
	printf("+-----------------------------+\n");
	printf("|       TASK STATISTICS       |\n");

	// Iterate all the tasktypes, no need for lock as runtime is shutting down
	list_head_t *list = task_type_manager_get_list();
	list_head_t *head;
	list_for_each(head, list) {
		nosv_task_type_t type = list_elem(head, struct nosv_task_type, list_hook);
		if (type != NULL) {
			// Display monitoring-related statistics
			tasktype_stats_t *type_stats = type->stats;
			size_t num_instances = tasktype_stats_get_num_instances(type_stats);
			if (num_instances) {
				printf("+-----------------------------+\n");

				double mean     = tasktype_stats_get_timing_mean(type_stats);
				double stddev   = tasktype_stats_get_timing_stddev(type_stats);
				double accuracy = tasktype_stats_get_timing_accuracy(type_stats);

				char type_label[80];
				if (type->label == NULL) {
					sprintf(type_label, "%s(%zu)", "Unlabeled", num_instances);
				} else {
					sprintf(type_label, "%s(%zu)", type->label, num_instances);
				}

				// Make sure there was at least one prediction to report accuracy
				char accuracy_label[80];
				if (!isnan(accuracy) && accuracy != 0.0) {
					sprintf(accuracy_label, "%lf%%", accuracy);
				} else {
					sprintf(accuracy_label, "%s", "NA");
				}

				printf("STATS  MONITORING  TASKTYPE(INSTANCES)  %s\n", type_label);
				printf("STATS  MONITORING  AVG NORMALIZED COST  %lf\n", mean);
				printf("STATS  MONITORING  STD NORMALIZED COST  %lf\n", stddev);
				printf("STATS  MONITORING  PREDICTION ACCURACY  %s\n", accuracy_label);
				printf("+-----------------------------+\n");
			}

			// Display hardware counters related statistics
			const enum counters_t *enabled_counters = hwcounters_get_enabled_counters();
			const size_t num_enabled_counters = hwcounters_get_num_enabled_counters();
			for (size_t id = 0; id < num_enabled_counters; ++id) {
				size_t num_instances = tasktype_stats_get_counter_num_instances(type_stats, id);
				if (num_instances) {
					// Get statistics
					double counter_sum = tasktype_stats_get_counter_sum(type_stats, id);
					double counter_avg = tasktype_stats_get_counter_average(type_stats, id);
					double counter_stddev = tasktype_stats_get_counter_stddev(type_stats, id);
					double counter_accuracy = tasktype_stats_get_counter_accuracy(type_stats, id);

					// Make sure there was at least one prediction to report accuracy
					char accuracy_label[80];
					if (!isnan(counter_accuracy) && counter_accuracy != 0.0) {
						sprintf(accuracy_label, "%lf%%", counter_accuracy);
					} else {
						sprintf(accuracy_label, "%s", "NA");
					}

					enum counters_t type_counter = enabled_counters[id];
					printf("STATS  HWCOUNTERS  SUM %s  %lf\n", counter_descriptions[type_counter].descr, counter_sum);
					printf("STATS  HWCOUNTERS  AVG %s  %lf\n", counter_descriptions[type_counter].descr, counter_avg);
					printf("STATS  HWCOUNTERS  STD %s  %lf\n", counter_descriptions[type_counter].descr, counter_stddev);
					printf("STATS  HWCOUNTERS  PREDICTION ACCURACY  %s\n", accuracy_label);
					printf("+-----------------------------+\n");
				}
			}

			printf("\n");
		}
	}

	printf("\n");
}

#endif // TASKMONITOR_H
