/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef TASKMONITOR_H
#define TASKMONITOR_H

#include <math.h>
#include <stdio.h>

#include "nosv-internal.h"

#include "taskstatistics.h"
#include "tasktypestatistics.h"
#include "system/tasks.h"


//! \brief Initialize a task's monitoring statistics
//! \param[in,out] task The task
static inline void taskmonitor_task_created(nosv_task_t task)
{
	assert(task != NULL);

	taskstatistics_t *task_stats = task->stats;
	assert(task_stats != NULL);

	// Initialize attributes of the new task
	taskstatistics_init(task_stats);
	size_t cost = 1; //TODO: task_get_cost(task
	task_stats->cost = cost;

	// Predict metrics using past data
	tasktypestatistics_t *type_stats = task->type->stats;
	double time_prediction = tasktypestatistics_get_timing_prediction(type_stats, cost);
	if (time_prediction != PREDICTION_UNAVAILABLE) {
		taskstatistics_set_time_prediction(task_stats, time_prediction);
	}

	// Set the task's tasktype statistics for future references
	task_stats->tasktypestats = type_stats;
}


static inline void taskmonitor_task_started(nosv_task_t task, enum monitoring_status_t status)
{
	assert(task != NULL);

	taskstatistics_t *task_stats = task->stats;
	assert(task_stats != NULL);

	// Start recording time for the new execution status
	enum monitoring_status_t old_status = taskstatistics_start_timing(task_stats, status);

	// If this is the first time the task becomes ready, increase the cost
	// accumulations used to infer predictions
	if (old_status == null_status && status == ready_status) {
		tasktypestatistics_t *type_stats = task_stats->tasktypestats;
		if (taskstatistics_has_time_prediction(task_stats)) {
			tasktypestatistics_increase_accumulated_cost(type_stats, task_stats->cost);
		} else {
			tasktypestatistics_increase_num_predictionless_instances(type_stats);
		}
	}
}

static inline void taskmonitor_task_finished(nosv_task_t task)
{
	// NOTE: No need for task completed user code, as tasks don't wait for their children
	assert(task != NULL);

	taskstatistics_t *task_stats = task->stats;
	assert(task_stats != NULL);

	// Stop timing for the task
	taskstatistics_stop_timing(task_stats);

	// Accumulate timing statistics into the task's type
	tasktypestatistics_t *type_stats = task_stats->tasktypestats;
	tasktypestatistics_accumulate(type_stats, task_stats);

	if (taskstatistics_has_time_prediction(task_stats)) {
		tasktypestatistics_decrease_accumulated_cost(type_stats, task_stats->cost);
	} else {
		tasktypestatistics_decrease_num_predictionless_instances(type_stats);
	}
}

static inline void taskmonitor_statistics()
{
	printf("+-----------------------------+\n");
	printf("|       TASK STATISTICS       |\n");
	printf("+-----------------------------+\n");

	// Iterate all the tasktypes, no need for lock as runtime is shutting down
	list_head_t *list = task_type_manager_get_list();
	list_head_t *head = list_front(list);
	list_head_t *stop = head;
	do {
		nosv_task_type_t type = list_elem(head, struct nosv_task_type, list_hook);
		if (type != NULL) {
			// Display monitoring-related statistics
			tasktypestatistics_t *type_stats = type->stats;
			size_t num_instances = tasktypestatistics_get_num_instances(type_stats);
			if (num_instances) {
				double mean     = tasktypestatistics_get_timing_mean(type_stats);
				double stddev   = tasktypestatistics_get_timing_stddev(type_stats);
				double accuracy = tasktypestatistics_get_timing_accuracy(type_stats);

				char type_label[80];
				if (type->label == NULL) {
					snprintf(type_label, 80, "%s(%zu)", "Unlabeled", num_instances);
				} else {
					snprintf(type_label, 80, "%s(%zu)", type->label, num_instances);
				}

				// Make sure there was at least one prediction to report accuracy
				char accuracy_label[80];
				if (!isnan(accuracy) && accuracy != 0.0) {
					snprintf(accuracy_label, 80, "%lf%%", accuracy);
				} else {
					snprintf(accuracy_label, 80, "%s", "NA");
				}

				printf("STATS  MONITORING  TASKTYPE(INSTANCES)  %s\n", type_label);
				printf("STATS  MONITORING  AVG NORMALIZED COST  %lf\n", mean);
				printf("STATS  MONITORING  STD NORMALIZED COST  %lf\n", stddev);
				printf("STATS  MONITORING  PREDICTION ACCURACY  %s\n", accuracy_label);
			}
			printf("+-----------------------------+\n");
		}

		head = list_next_circular(head, list);
	} while (head != stop);

	printf("\n");
}

#endif // TASKMONITOR_H
