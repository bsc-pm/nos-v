/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#include "climits.h"
#include "nosv-internal.h"
#include "hardware/cpus.h"
#include "hardware/threads.h"
#include "memory/sharedmemory.h"
#include "scheduler/scheduler.h"

scheduler_t *scheduler;
thread_local scheduler_queue_t *last;

void scheduler_init(int initialize)
{
	if (!initialize) {
		scheduler = (scheduler_t *)st_config.config->scheduler_ptr;
		return;
	}

	int cpu_count = cpus_count();

	scheduler = (scheduler_t *)salloc(sizeof(scheduler_t), -1);
	assert(scheduler);
	st_config.config->scheduler_ptr = scheduler;

	dtlock_init(&scheduler->dtlock, cpu_count * 2);
	scheduler->in_queue = spsc_alloc(IN_QUEUE_SIZE);
	list_init(&scheduler->queues);
	nosv_spin_init(&scheduler->in_lock);
	scheduler->tasks = 0;

	for (int i = 0; i < MAX_PIDS; ++i)
		scheduler->queues_direct[i] = NULL;

	scheduler->timestamps = (timestamp_t *)salloc(sizeof(timestamp_t) * cpu_count, -1);

	for (int i = 0; i < cpu_count; ++i) {
		scheduler->timestamps[i].pid = -1;
		scheduler->timestamps[i].ts_ns = 0;
	}
}

static inline scheduler_queue_t *scheduler_init_queue(int pid)
{
	assert(!scheduler->queues_direct[pid]);

	scheduler_queue_t *queue = salloc(sizeof(scheduler_queue_t), cpu_get_current());
	queue->pid = pid;
	list_init(&queue->tasks);
	scheduler->queues_direct[pid] = queue;
	list_add_tail(&scheduler->queues, &queue->list_hook);

	return queue;
}

// Must be called inside the dtlock
static inline void scheduler_process_ready_tasks()
{
	nosv_task_t task;

	// Could creators overflow this?
	// TODO maybe we want to limit how many tasks we pop
	while (spsc_pop(scheduler->in_queue, (void **)&task)) {
		scheduler->tasks++;

		assert(task);
		int pid = task->type->pid;
		scheduler_queue_t *pidqueue = scheduler->queues_direct[pid];

		if (!pidqueue)
			pidqueue = scheduler_init_queue(pid);

		list_add_tail(&pidqueue->tasks, &task->list_hook);
	}
}

/* This function returns 1 if the current PID has spent more time than the quantum */
static inline int scheduler_should_yield(int pid, int cpu, uint64_t *timestamp)
{
	struct timespec tp;
	clock_gettime(CLK_SRC, &tp);
	*timestamp = tp.tv_sec * 1000000000 + tp.tv_nsec;

	if (scheduler->timestamps[cpu].pid != pid) {
		return 0;
	}

	if ((*timestamp - scheduler->timestamps[cpu].ts_ns) > QUANTUM_NS)
		return 1;

	return 0;
}

static inline void scheduler_update_ts(int pid, nosv_task_t task, int cpu, uint64_t timestamp)
{
	if (!task) {
		scheduler->timestamps[cpu].pid = -1;
		return;
	}

	int task_pid = task->type->pid;
	if (task->type->pid != pid) {
		scheduler->timestamps[cpu].pid = task_pid;
		scheduler->timestamps[cpu].ts_ns = timestamp;
		return;
	}

	if (pid != scheduler->timestamps[cpu].pid) {
		scheduler->timestamps[cpu].pid = pid;
		scheduler->timestamps[cpu].ts_ns = timestamp;
		return;
	}
}

void scheduler_submit(nosv_task_t task)
{
	assert(task);
	assert(scheduler);

	int success = 0;

	while (!success) {
		nosv_spin_lock(&scheduler->in_lock);
		success = spsc_push(scheduler->in_queue, (void *)task);
		nosv_spin_unlock(&scheduler->in_lock);

		int lock = dtlock_try_lock(&scheduler->dtlock);
		if (lock) {
			scheduler_process_ready_tasks();
			dtlock_unlock(&scheduler->dtlock);
		}
	}
}

// Very basic
// TODO: Priority, quantum
nosv_task_t scheduler_get_internal(int cpu)
{
	int pid, yield;
	uint64_t ts;

	if (!scheduler->tasks) {
		scheduler_update_ts(0, NULL, cpu, 0);
		return NULL;
	}

	// Once we are here, we know for sure there is at least one task in the scheduler
	scheduler->tasks--;

	// What PID is that CPU running?
	pid = cpu_get_pid(cpu);
	scheduler_queue_t *queue = scheduler->queues_direct[pid];

	// Do we need to yield?
	yield = scheduler_should_yield(pid, cpu, &ts);

	list_head_t *it = &queue->list_hook;

	if (yield) {
		// What we do is grab the current queue, that corresponds to our pid, and traverse that forward, to cause a round-robin.
		// If we get to the end, we start from the beginning. Most importantly, if we arrive at a point where we go back to our queue, it's fine.
		// Otherwise, we will begin from the current position, which means we will try our process first
		it = list_next_circular(it, &scheduler->queues);
	}

	while (1) {
		queue = list_elem(it, scheduler_queue_t, list_hook);

		list_head_t *head = list_pop_head(&queue->tasks);
		if (head) {
			nosv_task_t task = list_elem(head, struct nosv_task, list_hook);
			assert(task);
			scheduler_update_ts(pid, task, cpu, ts);
			return task;
		}

		it = list_next_circular(it, &scheduler->queues);
	}

	// Cannot happen
	assert(0);
	return NULL;
}

nosv_task_t scheduler_get(int cpu)
{
	assert(cpu >= 0);

	void *item;
	if (!dtlock_lock_or_delegate(&scheduler->dtlock, (uint64_t)cpu, &item)) {
		// Served item
		return (nosv_task_t)item;
	}

	// Lock acquired
	nosv_task_t task = NULL;

	do {
		scheduler_process_ready_tasks();

		size_t served = 0;
		while (served < MAX_SERVED_TASKS && !dtlock_empty(&scheduler->dtlock)) {
			uint64_t cpu_delegated = dtlock_front(&scheduler->dtlock);
			assert(cpu_delegated < cpus_count());

			task = scheduler_get_internal(cpu_delegated);
			dtlock_set_item(&scheduler->dtlock, cpu_delegated, task);
			dtlock_popfront(&scheduler->dtlock);

			served++;
			if (!task)
				break;
		}

		// Work for myself
		task = scheduler_get_internal(cpu);
	} while (!task && !worker_should_shutdown());

	dtlock_unlock(&scheduler->dtlock);

	return task;
}
