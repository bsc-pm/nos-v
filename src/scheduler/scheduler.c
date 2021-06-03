/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <stddef.h>

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

	scheduler = (scheduler_t *)salloc(sizeof(scheduler_t), -1);
	assert(scheduler);
	st_config.config->scheduler_ptr = scheduler;

	dtlock_init(&scheduler->dtlock, cpus_count() * 2);
	scheduler->in_queue = spsc_alloc(IN_QUEUE_SIZE);
	list_init(&scheduler->queues);
	nosv_spin_init(&scheduler->in_lock);
	scheduler->tasks = 0;

	for (int i = 0; i < MAX_PIDS; ++i)
		scheduler->queues_direct[i] = NULL;
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
	if (!scheduler->tasks)
		return NULL;

	scheduler->tasks--;

	// What PID is that CPU running?
	int pid = cpu_get_pid(cpu);

	// 1. Find the queue of that PID
	scheduler_queue_t *queue = scheduler->queues_direct[pid];

	if (queue) {
		list_head_t *head = list_pop_head(&queue->tasks);
		if (head) {
			int a = offsetof(struct nosv_task, list_hook);
			return list_elem(head, struct nosv_task, list_hook);
		}
	}

	// 2. Iterate the rest of the queues
	list_head_t *qhead = list_front(&scheduler->queues);

	while (qhead) {
		queue = list_elem(qhead, scheduler_queue_t, list_hook);

		list_head_t *head = list_pop_head(&queue->tasks);
		if (head) {
			return list_elem(head, struct nosv_task, list_hook);
		}

		qhead = list_next(qhead);
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
