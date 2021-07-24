/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "climits.h"
#include "compiler.h"
#include "nosv-internal.h"
#include "generic/clock.h"
#include "hardware/cpus.h"
#include "hardware/threads.h"
#include "hardware/locality.h"
#include "memory/sharedmemory.h"
#include "scheduler/scheduler.h"

__internal scheduler_t *scheduler;
__internal thread_local process_scheduler_t *last;

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
	scheduler->served_tasks = 0;

	for (int i = 0; i < MAX_PIDS; ++i)
		scheduler->queues_direct[i] = NULL;

	scheduler->timestamps = (timestamp_t *)salloc(sizeof(timestamp_t) * cpu_count, -1);

	for (int i = 0; i < cpu_count; ++i) {
		scheduler->timestamps[i].pid = -1;
		scheduler->timestamps[i].ts_ns = 0;
	}
}

static inline void scheduler_init_queue(scheduler_queue_t *queue)
{
	list_init(&queue->tasks);
}

static inline process_scheduler_t *scheduler_init_pid(int pid)
{
	assert(!scheduler->queues_direct[pid]);

	process_scheduler_t *sched = salloc(sizeof(process_scheduler_t), cpu_get_current());

	int cpus = cpus_count();
	sched->per_cpu_queue_preferred = salloc(sizeof(scheduler_queue_t) * cpus, cpu_get_current());
	sched->per_cpu_queue_strict = salloc(sizeof(scheduler_queue_t) * cpus, cpu_get_current());

	for (int i = 0; i < cpus; ++i) {
		scheduler_init_queue(&sched->per_cpu_queue_preferred[i]);
		scheduler_init_queue(&sched->per_cpu_queue_strict[i]);
	}

	int numas = locality_numa_count();
	sched->per_numa_queue_preferred = salloc(sizeof(scheduler_queue_t) * numas, cpu_get_current());
	sched->per_numa_queue_strict = salloc(sizeof(scheduler_queue_t) * numas, cpu_get_current());

	for (int i = 0; i < numas; ++i) {
		scheduler_init_queue(&sched->per_numa_queue_preferred[i]);
		scheduler_init_queue(&sched->per_numa_queue_strict[i]);
	}

	sched->pid = pid;
	scheduler_init_queue(&sched->queue);
	scheduler->queues_direct[pid] = sched;
	list_add_tail(&scheduler->queues, &sched->list_hook);

	heap_init(&sched->deadline_tasks);
	scheduler_init_queue(&sched->yield_tasks);
	sched->now = clock_ns();

	return sched;
}

static inline int deadline_cmp(heap_node_t *a, heap_node_t *b)
{
	nosv_task_t task_a = heap_elem(a, struct nosv_task, heap_hook);
	nosv_task_t task_b = heap_elem(b, struct nosv_task, heap_hook);

	return task_b->deadline - task_a->deadline;
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
		process_scheduler_t *pidqueue = scheduler->queues_direct[pid];

		if (!pidqueue)
			pidqueue = scheduler_init_pid(pid);

		if (task->yield) {
			assert(!task->deadline);
			// This yield task will be executed when either all
			// tasks in the global scheduler have been run or when
			// there is no more work to do other than yield tasks
			task->yield = scheduler->served_tasks + scheduler->tasks;
			list_add_tail(&pidqueue->yield_tasks.tasks, &task->list_hook);
		} else if (task->deadline) {
			heap_insert(&pidqueue->deadline_tasks, &task->heap_hook, &deadline_cmp);
		} else {
			list_add_tail(&pidqueue->queue.tasks, &task->list_hook);
		}

		pidqueue->tasks++;
	}
}

/* This function returns 1 if the current PID has spent more time than the quantum */
static inline int scheduler_should_yield(int pid, int cpu, uint64_t *timestamp)
{
	*timestamp = clock_fast_ns();

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

		if (!success) {
			int lock = dtlock_try_lock(&scheduler->dtlock);
			if (lock) {
				scheduler_process_ready_tasks();
				dtlock_unlock(&scheduler->dtlock);
			}
		}
	}
}

static inline int scheduler_find_task_queue(scheduler_queue_t *queue, nosv_task_t *task /*out*/)
{
	list_head_t *head = list_pop_head(&queue->tasks);

	if (head) {
		*task = list_elem(head, struct nosv_task, list_hook);
		return 1;
	}

	return 0;
}

static inline int task_affine(nosv_task_t task, cpu_t *cpu)
{
	switch (task->affinity.level) {
		case CPU:
			return task->affinity.index == cpu->system_id;
		case NUMA:
			return locality_get_logical_numa(task->affinity.index) == cpu->numa_node;
		case USER_COMPLEX:
		default:
			return 1;
	}
}

// Insert the task to the relevant queue
static inline void scheduler_insert_affine(process_scheduler_t *sched, nosv_task_t task)
{
	assert(task->affinity.level != NONE);
	assert(task->affinity.level != USER_COMPLEX);
	scheduler_queue_t *queue = NULL;
	int idx;

	switch (task->affinity.level) {
		case CPU:
			idx = cpu_system_to_logical(task->affinity.index);
			assert(idx >= 0);
			queue = (task->affinity.type == STRICT)
						? &sched->per_cpu_queue_strict[idx]
						: &sched->per_cpu_queue_preferred[idx];
			break;
		case NUMA:
			idx = locality_get_logical_numa(task->affinity.index);
			queue = (task->affinity.type == STRICT)
						? &sched->per_numa_queue_strict[idx]
						: &sched->per_numa_queue_preferred[idx];
			break;
		default:
			break;
	}

	assert(queue != NULL);
	list_add_tail(&queue->tasks, &task->list_hook);
}

static inline int scheduler_get_yield_expired(process_scheduler_t *sched, nosv_task_t *task /*out*/)
{
	nosv_task_t res;
	list_head_t *head = list_front(&sched->yield_tasks.tasks);

	if (!head)
		return 0;

	res = list_elem(head, struct nosv_task, list_hook);
	if (res->yield <= scheduler->served_tasks) {
		list_pop_head(&sched->yield_tasks.tasks);
		res->yield = 0;
		*task = res;
		return 1;
	}

	return 0;
}

static inline int scheduler_get_deadline_expired(process_scheduler_t *sched, nosv_task_t *task /*out*/)
{
	// In reality we get the minimum, as we inverted the comparison function.
	heap_node_t *head = heap_max(&sched->deadline_tasks);

	// No deadline tasks
	if (!head)
		return 0;

	nosv_task_t res = heap_elem(head, struct nosv_task, heap_hook);

	if (res->deadline < sched->now) {
		goto deadline_expired;
	} else {
		// Update timestamp just in case
		sched->now = clock_ns();

		if (res->deadline < sched->now) {
			goto deadline_expired;
		}
	}

	return 0;

deadline_expired:
	heap_pop_max(&sched->deadline_tasks, &deadline_cmp);
	res->deadline = 0;
	*task = res;
	return 1;
}

static inline nosv_task_t scheduler_find_task_process(process_scheduler_t *sched, cpu_t *cpu)
{
	int cpuid = cpu->logic_id;
	nosv_task_t task = NULL;

	// Are there any tasks?
	if (!sched->tasks)
		return NULL;

	// Check deadlines
	while (scheduler_get_deadline_expired(sched, &task)) {
		// Check if the task is affine with the current cpu
		if (task_affine(task, cpu))
			goto task_obtained;

		// Not affine. Insert to an appropiate queue
		scheduler_insert_affine(sched, task);
	}

	// Check yield
	while (scheduler_get_yield_expired(sched, &task)) {
		// Check if the task is affine with the current cpu
		if (task_affine(task, cpu))
			goto task_obtained;

		// Not affine. Insert to an appropiate queue
		scheduler_insert_affine(sched, task);
	}

	// We'll decrease the pointer now, but if we decide to not grab any task we have to increment it back again.
	// If we obtain a task, we have to decrement the task count and return the pointer
	if (scheduler_find_task_queue(&sched->per_cpu_queue_strict[cpuid], &task))
		goto task_obtained;

	if (scheduler_find_task_queue(&sched->per_cpu_queue_preferred[cpuid], &task))
		goto task_obtained;

	if (scheduler_find_task_queue(&sched->per_numa_queue_strict[cpu->numa_node], &task))
		goto task_obtained;

	if (scheduler_find_task_queue(&sched->per_numa_queue_preferred[cpu->numa_node], &task))
		goto task_obtained;

	while (scheduler_find_task_queue(&sched->queue, &task)) {
		// Check the task is affine with the current cpu
		if (task_affine(task, cpu))
			goto task_obtained;

		// Not affine. Insert to an appropiate queue
		scheduler_insert_affine(sched, task);
	}

	int cpus = cpus_count();
	int numas = locality_numa_count();
	// We can try to steal from somewhere
	// Note that we don't skip our own cpus, although we know nothing is there, just for simplicity
	for (int i = 0; i < cpus; ++i) {
		if (scheduler_find_task_queue(&sched->per_cpu_queue_preferred[i], &task))
			goto task_obtained;
	}

	for (int i = 0; i < numas; ++i) {
		if (scheduler_find_task_queue(&sched->per_numa_queue_preferred[i], &task))
			goto task_obtained;
	}

	// If we get here, we didn't find any tasks, otherwise we would've gone to task_obtained
	return NULL;

task_obtained:
	sched->tasks--;
	return task;
}

static inline nosv_task_t scheduler_find_task_yield_process(process_scheduler_t *sched, cpu_t *cpu)
{
	nosv_task_t task;

	// Are there any yield tasks?
	list_head_t *elem = list_front(&sched->yield_tasks.tasks);
	if (!elem)
		return NULL;

	assert(sched->tasks > 0);

	do {
		task = list_elem(elem, struct nosv_task, list_hook);
		if (task_affine(task, cpu)) {
			list_remove(&sched->yield_tasks.tasks, elem);
			sched->tasks--;
			task->yield = 0;
			return task;
		}
	} while ((elem = list_next(elem)));

	return NULL;
}

static inline nosv_task_t scheduler_get_internal(int cpu)
{
	int pid, yield;
	uint64_t ts;
	list_head_t *it;

	if (!scheduler->tasks) {
		scheduler_update_ts(0, NULL, cpu, 0);
		return NULL;
	}

	cpu_t *cpu_str = cpu_get(cpu);

	// What PID is that CPU running?
	pid = cpu_get_pid(cpu);
	process_scheduler_t *sched = scheduler->queues_direct[pid];

	// sched may be NULL at this point
	// Maybe that CPU doesn't have a process_scheduler_t initialized already,
	// hence, in that case, grab the standard list.
	// The list itself must have at least one process_scheduler_t, as there are tasks in the scheduler.
	if (sched)
		it = &sched->list_hook;
	else
		it = list_front(&scheduler->queues);

	// Do we need to yield?
	yield = scheduler_should_yield(pid, cpu, &ts);

	if (yield) {
		// What we do is grab the current queue, that corresponds to our pid, and traverse that forward, to cause a round-robin.
		// If we get to the end, we start from the beginning. Most importantly, if we arrive at a point where we go back to our queue, it's fine.
		// Otherwise, we will begin from the current position, which means we will try our process first
		it = list_next_circular(it, &scheduler->queues);
	}

	list_head_t *stop = it;

	// Search for a ready task to run. If none is found in the current
	// scheduler, search a ready task in the next scheduler in the list.
	do {
		sched = list_elem(it, process_scheduler_t, list_hook);

		nosv_task_t task = scheduler_find_task_process(sched, cpu_str);

		if (task) {
			scheduler->tasks--;
			scheduler->served_tasks++;
			scheduler_update_ts(pid, task, cpu, ts);
			return task;
		}

		it = list_next_circular(it, &scheduler->queues);
	} while (it != stop);

	// If we have not been able to find any ready task suitable to be run in
	// this cpu, search for the first yield task we can find, even if it has
	// not "expired" yet. It is ok for this double search to be somewhat
	// redundant, we have nothing better to do in this cpu.

	// We cannot start the search in our own process scheduler, if there is
	// a yield task there, we would run it in a loop instead of trying other
	// processes.
	if (!yield) {
		it = list_next_circular(it, &scheduler->queues);
		stop = it;
	}

	do {
		sched = list_elem(it, process_scheduler_t, list_hook);

		nosv_task_t task = scheduler_find_task_yield_process(sched, cpu_str);

		if (task) {
			scheduler->tasks--;
			scheduler->served_tasks++;
			scheduler_update_ts(pid, task, cpu, ts);
			return task;
		}

		it = list_next_circular(it, &scheduler->queues);
	} while (it != stop);

	// This may only happen if there are strict bindings that we couldn't steal
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
