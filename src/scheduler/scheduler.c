/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "common.h"
#include "compiler.h"
#include "config/config.h"
#include "defaults.h"
#include "generic/clock.h"
#include "generic/list.h"
#include "hardware/cpus.h"
#include "hardware/locality.h"
#include "hardware/threads.h"
#include "instr.h"
#include "memory/sharedmemory.h"
#include "scheduler/scheduler.h"
#include "system/taskgroup.h"
#include "system/tasks.h"

__internal scheduler_t *scheduler;
__internal thread_local process_scheduler_t *last;
__internal nosv_task_t *task_batch_buffer;

static inline int task_priority_compare(nosv_task_t a, nosv_task_t b)
{
	return (a->priority > b->priority) - (b->priority > a->priority);
}

static inline int task_deadline_compare(nosv_task_t a, nosv_task_t b)
{
	return (a->deadline > b->deadline) - (b->deadline > a->deadline);
}

RB_PROTOTYPE_STATIC(priority_tree, nosv_task, tree_hook, task_priority_compare)
RB_GENERATE_STATIC(priority_tree, nosv_task, tree_hook, task_priority_compare)

RB_PROTOTYPE_STATIC(deadline_tree, nosv_task, tree_hook, task_deadline_compare)
RB_GENERATE_STATIC(deadline_tree, nosv_task, tree_hook, task_deadline_compare)

void scheduler_init(int initialize)
{
	task_batch_buffer = malloc(sizeof(nosv_task_t) * nosv_config.sched_batch_size);
	assert(task_batch_buffer);

	if (!initialize) {
		scheduler = (scheduler_t *) st_config.config->scheduler_ptr;
		return;
	}

	int cpu_count = cpus_count();

	scheduler = (scheduler_t *) salloc(sizeof(scheduler_t), -1);
	assert(scheduler);
	st_config.config->scheduler_ptr = scheduler;

	dtlock_init(&scheduler->dtlock, cpu_count * 2);
	governor_init(&scheduler->governor);

	scheduler->in_queue = mpsc_alloc(cpu_count, nosv_config.sched_in_queue_size);
	list_init(&scheduler->queues);
	scheduler->tasks = 0;
	scheduler->served_tasks = 0;
	atomic_init(&scheduler->deadline_purge, 0);

	for (int i = 0; i < MAX_PIDS; ++i)
		scheduler->queues_direct[i] = NULL;

	scheduler->timestamps = (timestamp_t *) salloc(sizeof(timestamp_t) * cpu_count, -1);

	for (int i = 0; i < cpu_count; ++i) {
		scheduler->timestamps[i].pid = -1;
		scheduler->timestamps[i].ts_ns = 0;
	}

	scheduler->quantum_ns = nosv_config.sched_quantum_ns;
}

void scheduler_wake(int pid)
{
	process_scheduler_t *sched = scheduler->queues_direct[pid];

	if (sched)
		atomic_fetch_add_explicit(&sched->shutdown, 1, memory_order_relaxed);
}

void scheduler_shutdown(void)
{
	free(task_batch_buffer);
}

static inline void scheduler_init_queue(scheduler_queue_t *queue)
{
	list_init(&queue->tasks);
	queue->priority_enabled = 0;
}

static inline int scheduler_find_in_queue(scheduler_queue_t *queue, nosv_task_t *task /*out*/)
{
	if (queue->priority_enabled) {
		nosv_task_t t = RB_MAX(priority_tree, &queue->tasks_priority);

		if (t) {
			*task = t;
			return 1;
		}
	} else {
		list_head_t *head = list_front(&queue->tasks);

		if (head) {
			*task = list_elem(head, struct nosv_task, list_hook);
			return 1;
		}
	}

	return 0;
}

static inline void scheduler_pop_queue(scheduler_queue_t *queue, nosv_task_t task)
{
	assert(task);
	if (queue->priority_enabled) {
		list_head_t *next = list_front(&task->list_hook);
		nosv_task_t next_task = list_elem(next, struct nosv_task, list_hook);
		if (next) {
			// Change the list "head" to be the next task.
			list_remove(&task->list_hook);
			// Now transplant the element to replace the current element in the Red-Black Tree
			RB_TRANSPLANT(priority_tree, &queue->tasks_priority, task, next_task);
		} else {
			RB_REMOVE(priority_tree, &queue->tasks_priority, task);
		}

		// Clean the tree hooks
		memset(&task->tree_hook, 0, sizeof(task->tree_hook));
	} else {
		__maybe_unused list_head_t *head = list_pop_front(&queue->tasks);
		assert(head == &task->list_hook);
	}

	// Make sure this hook is not linked to any other task
	list_init(&(task->list_hook));
}

static inline int scheduler_get_from_queue(scheduler_queue_t *queue, nosv_task_t *task /*out*/, int *removed /*out*/)
{
	assert(removed);
	assert(*removed == 1);

	if (scheduler_find_in_queue(queue, task)) {
		nosv_task_t t = *task;
		int32_t degree = atomic_load_explicit(&(t->degree), memory_order_relaxed);

		// The only cases where scheduled_count may not be zero here
		// are when dealing with parallel tasks or blocked tasks
		assert(task_is_parallel(t) || t->worker || t->scheduled_count == 0);
		t->scheduled_count++;
		assert(t->scheduled_count > 0);

		if (degree < 0 || t->scheduled_count >= degree) {
			scheduler_pop_queue(queue, t);

			// Cancelled task
			// Add to removed counter all the pending executions of this task
			if (degree < 0) {
				int original_degree = -degree;
				assert(original_degree >= t->scheduled_count);
				*removed += (original_degree - t->scheduled_count);
			}
		} else {
			atomic_fetch_add_explicit(&t->event_count, 1, memory_order_relaxed);
		}

		return 1;
	}

	return 0;
}

static inline void scheduler_add_queue(scheduler_queue_t *queue, nosv_task_t task)
{
	if (queue->priority_enabled) {
		nosv_task_t found = RB_FIND(priority_tree, &queue->tasks_priority, task);

		if (found) {
			list_add_tail(&found->list_hook, &task->list_hook);
		} else {
			// Insert the new task with priority
			list_init(&task->list_hook);
			RB_INSERT(priority_tree, &queue->tasks_priority, task);
		}
	} else {
		if (unlikely(task->priority)) {
			// Enabling priorities
			queue->priority_enabled = 1;
			list_head_t previous;
			list_init(&previous);
			list_replace(&queue->tasks, &previous);
			RB_INIT(&queue->tasks_priority);
			list_head_t *new_head = list_front(&previous);

			if (new_head) {
				// There are elements in the current list
				list_remove(&previous);
				RB_INSERT(priority_tree, &queue->tasks_priority, list_elem(new_head, struct nosv_task, list_hook));
			}

			// Insert the new task with priority
			list_init(&task->list_hook);
			RB_INSERT(priority_tree, &queue->tasks_priority, task);
		} else {
			// Fast path
			list_add_tail(&queue->tasks, &task->list_hook);
		}
	}
}

static inline process_scheduler_t *scheduler_init_pid(int pid)
{
	assert(!scheduler->queues_direct[pid]);

	process_scheduler_t *sched = salloc(sizeof(process_scheduler_t), cpu_get_current());
	sched->preferred_affinity_tasks = 0;
	sched->tasks = 0;

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

	RB_INIT(&sched->deadline_tasks);
	list_init(&sched->yield_tasks.tasks);
	sched->now = clock_ns();

	sched->last_shutdown = 0;
	atomic_init(&sched->shutdown, 0);

	return sched;
}

// Check if any schedulers need shutting down
static inline void scheduler_check_process_shutdowns(void)
{
	list_head_t *head;
	list_for_each(head, &scheduler->queues) {
		process_scheduler_t *sched = list_elem(head, process_scheduler_t, list_hook);
		int shutdown = atomic_load_explicit(&sched->shutdown, memory_order_relaxed);
		if (shutdown > sched->last_shutdown) {
			governor_shutdown_process(&scheduler->governor, sched->pid, &scheduler->dtlock);
			sched->last_shutdown = shutdown;
		}
	}
}

static inline void scheduler_insert_ready_task(nosv_task_t task)
{
	int pid = task->type->pid;
	process_scheduler_t *pidqueue = scheduler->queues_direct[pid];
	int degree = task_get_degree(task);
	assert(degree > 0);

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
		deadline_state_t expected = NOSV_DEADLINE_PENDING;
		deadline_state_t desired = NOSV_DEADLINE_WAITING;
		if (atomic_compare_exchange_strong_explicit(&task->deadline_state, &expected, desired, memory_order_relaxed, memory_order_relaxed)) {
			// Two tasks with the same deadline cannot exist inside the rb-tree.
			// If this happens, increase the deadline and retry
			while (RB_INSERT(deadline_tree, &pidqueue->deadline_tasks, task))
				task->deadline++;
		} else {
			assert(expected == NOSV_DEADLINE_READY);
			// the task has been lapsed, enqueue it directly
			scheduler_add_queue(&pidqueue->queue, task);
		}
	} else {
		// Add to general queue. If this is an affinity task, it will then be removed and
		// added into one of the affine queues, but this way we don't give implicit priority
		// to affine tasks
		scheduler_add_queue(&pidqueue->queue, task);
	}

	pidqueue->tasks += degree;
	scheduler->tasks += degree;
}

static inline void scheduler_process_ready_task_buffer(nosv_task_t first_task)
{
	assert(first_task != NULL);

	nosv_task_t task = first_task;
	list_head_t *start = &(task->list_hook);
	list_head_t *iterator = start;
	do {
		task = list_elem(iterator, struct nosv_task, list_hook);
		iterator = list_next(iterator);
		list_init(&(task->list_hook));
		scheduler_insert_ready_task(task);
	} while (iterator != start);
}

// Must be called inside the dtlock
static inline void scheduler_process_ready_tasks(int fromServer)
{
	const uint64_t batch_size = nosv_config.sched_batch_size;
	size_t cnt = 0;

	// TODO maybe we want to limit how many tasks we pop
	while ((cnt = mpsc_pop_batch(scheduler->in_queue, (void **) task_batch_buffer, batch_size))) {
		for (size_t i = 0; i < cnt; ++i) {
			if (fromServer)
				instr_worker_progressing();

			assert(task_batch_buffer[i]);
			scheduler_process_ready_task_buffer(task_batch_buffer[i]);
		}
	}

	scheduler_check_process_shutdowns();
}

/* This function returns 1 if the current PID has spent more time than the quantum */
int scheduler_should_yield(int pid, int cpu, uint64_t *timestamp)
{
	*timestamp = clock_fast_ns();

	if (scheduler->timestamps[cpu].pid != pid) {
		return 0;
	}

	if ((*timestamp - scheduler->timestamps[cpu].ts_ns) > scheduler->quantum_ns)
		return 1;

	return 0;
}

void scheduler_reset_accounting(int pid, int cpu)
{
	// No valid PID other than the one being reset should have been running immediately before
	assert(scheduler->timestamps[cpu].pid == pid || scheduler->timestamps[cpu].pid == -1);

	scheduler->timestamps[cpu].pid = pid;
	scheduler->timestamps[cpu].ts_ns = clock_fast_ns();
}

static inline void scheduler_update_accounting(int pid, nosv_task_t task, int cpu, uint64_t timestamp)
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

void scheduler_batch_submit(nosv_task_t task)
{
	assert(task != NULL);

	nosv_task_t current_task = worker_current_task();

	if (!current_task || current_task->submit_window_maxsize == 1) {
		scheduler_submit_single(task);
		return;
	}

	task_group_add(&current_task->submit_window, task);
	if (task_group_count(&current_task->submit_window) >= current_task->submit_window_maxsize)
		nosv_flush_submit_window();
}

static inline void scheduler_submit_internal(nosv_task_t task)
{
	assert(task);
	assert(scheduler);

	int success = 0;

	instr_sched_submit_enter();

	while (!success) {
		success = mpsc_push(scheduler->in_queue, (void *) task, cpu_get_current());

		if (!success) {
			if (dtlock_try_lock(&scheduler->dtlock)) {
				scheduler_process_ready_tasks(0);
				dtlock_unlock(&scheduler->dtlock);
			}
		}
	}

	instr_sched_submit_exit();
}

void scheduler_submit_single(nosv_task_t task)
{
	assert(task);
	scheduler_submit_internal(task);
}

void scheduler_submit_group(task_group_t *group)
{
	assert(group);
	assert(!task_group_empty(group));
	scheduler_submit_internal(task_group_head(group));
}

static inline void scheduler_deadline_purge_internal(int count)
{
	list_head_t *it, *stop;
	nosv_task_t task, tmp;
	int to_purge = count;

	if (!scheduler->tasks)
		return;

	// Get this process scheduler
	process_scheduler_t *sched = scheduler->queues_direct[logic_pid];
	// sched may be NULL at this point. See scheduler_get_internal for more details
	it = (sched) ? &sched->list_hook : list_front(&scheduler->queues);
	stop = it;

	// Itreate over all processes deadline red-black trees and remove from
	// them up to "count" tasks. Add these tasks to the ready queues.
	do {
		sched = list_elem(it, process_scheduler_t, list_hook);
		if (!RB_EMPTY(&sched->deadline_tasks)) {
			// Tasks in the rb-tree should have the state
			// NOSV_DEADLINE_WAITING state. If a task has the
			// NOSV_DEADLINE_READY state, it means that it has been woken
			// up. Move it to the ready queues.
			RB_FOREACH_SAFE(task, deadline_tree, &sched->deadline_tasks, tmp) {
				deadline_state_t state = atomic_load_explicit(&task->deadline_state, memory_order_relaxed);
				if (state == NOSV_DEADLINE_READY) {
					RB_REMOVE(deadline_tree, &sched->deadline_tasks, task);
					task->deadline = 0;
					scheduler_add_queue(&sched->queue, task);
					to_purge--;
					if (!to_purge)
						break;
				}
			}
		}
		it = list_next_circular(it, &scheduler->queues);
	} while ((it != stop) && to_purge);
}

static inline void scheduler_deadline_purge(void)
{
	// Remove lapsed deadline tasks from the rbtree

	int to_purge = atomic_load_explicit(&scheduler->deadline_purge, memory_order_relaxed);
	if (to_purge) {
		// The acquire operation matches the release operation on
		// "scheduler_request_deadline_purge". Note that the
		// deadline_purge load above is relaxed to decrease the cost of
		// synchronization inside the dtlock main loop.
		atomic_thread_fence(memory_order_acquire);
		scheduler_deadline_purge_internal(to_purge);
		// Less than "to_purge" tasks might have been purged,
		// that is ok given that the remaining tasks might have
		// been naturally removed from the deadline tree when
		// its timeout expired.
		atomic_fetch_sub_explicit(&scheduler->deadline_purge, to_purge, memory_order_relaxed);
	}
}

void scheduler_request_deadline_purge(void)
{
	// The release operation matches the acquire operation on
	// "scheduler_deadline_purge". This makes sure that previously marked
	// tasks as "READY" will be seen after requesting a purge operation.
	atomic_fetch_add_explicit(&scheduler->deadline_purge, 1, memory_order_release);
}

int task_affine(nosv_task_t task, cpu_t *cpu)
{
	switch (task->affinity.level) {
		case NOSV_AFFINITY_LEVEL_CPU:
			return ((int) task->affinity.index) == cpu->system_id;
		case NOSV_AFFINITY_LEVEL_NUMA:
			return locality_get_logical_numa((int) task->affinity.index) == cpu->numa_node;
		case NOSV_AFFINITY_LEVEL_USER_COMPLEX:
		default:
			return 1;
	}
}

// Insert the task to the relevant queue
static inline void scheduler_insert_affine(process_scheduler_t *sched, nosv_task_t task)
{
	assert(task->affinity.level != NOSV_AFFINITY_LEVEL_NONE);
	assert(task->affinity.level != NOSV_AFFINITY_LEVEL_USER_COMPLEX);
	scheduler_queue_t *queue = NULL;
	int idx;

	switch (task->affinity.level) {
		case NOSV_AFFINITY_LEVEL_CPU:
			idx = cpu_system_to_logical((int) task->affinity.index);
			assert(idx >= 0);
			queue = (task->affinity.type == NOSV_AFFINITY_TYPE_STRICT)
						? &sched->per_cpu_queue_strict[idx]
						: &sched->per_cpu_queue_preferred[idx];
			break;
		case NOSV_AFFINITY_LEVEL_NUMA:
			idx = locality_get_logical_numa((int) task->affinity.index);
			queue = (task->affinity.type == NOSV_AFFINITY_TYPE_STRICT)
						? &sched->per_numa_queue_strict[idx]
						: &sched->per_numa_queue_preferred[idx];
			break;
		default:
			break;
	}

	assert(queue != NULL);
	if (task->affinity.type == NOSV_AFFINITY_TYPE_PREFERRED)
		sched->preferred_affinity_tasks++;

	scheduler_add_queue(queue, task);
}

static inline int scheduler_get_yield_expired(process_scheduler_t *sched, nosv_task_t *task /*out*/)
{
	nosv_task_t res;
	list_head_t *head = list_front(&sched->yield_tasks.tasks);

	if (!head)
		return 0;

	res = list_elem(head, struct nosv_task, list_hook);
	if (res->yield <= scheduler->served_tasks) {
		list_pop_front(&sched->yield_tasks.tasks);
		res->yield = 0;
		*task = res;
		return 1;
	}

	return 0;
}

static inline int scheduler_get_deadline_expired(process_scheduler_t *sched, nosv_task_t *task /*out*/)
{
	nosv_task_t res = RB_MIN(deadline_tree, &sched->deadline_tasks);

	// No deadline tasks
	if (!res)
		return 0;

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
	RB_REMOVE(deadline_tree, &sched->deadline_tasks, res);
	atomic_store_explicit(&res->deadline_state, NOSV_DEADLINE_READY, memory_order_relaxed);
	res->deadline = 0;
	*task = res;
	return 1;
}

static inline nosv_task_t scheduler_find_task_process(process_scheduler_t *sched, cpu_t *cpu, int *removed)
{
	int cpuid = cpu->logic_id;
	nosv_task_t task = NULL;

	*removed = 1;

	// Are there any tasks?
	if (!sched->tasks)
		return NULL;

	// Check deadlines
	while (scheduler_get_deadline_expired(sched, &task)) {
		assert(!task_is_parallel(task));

		// Check if the task is affine with the current cpu
		if (task_affine(task, cpu)) {
			task->scheduled_count++;
			goto task_obtained;
		}

		// Not affine. Insert to an appropiate queue
		scheduler_insert_affine(sched, task);
	}

	// Check yield
	while (scheduler_get_yield_expired(sched, &task)) {
		assert(!task_is_parallel(task));

		// Check if the task is affine with the current cpu
		if (task_affine(task, cpu)) {
			task->scheduled_count++;
			goto task_obtained;
		}

		// Not affine. Insert to an appropiate queue
		scheduler_insert_affine(sched, task);
	}

	// We'll decrease the pointer now, but if we decide to not grab any task we have to increment it back again.
	// If we obtain a task, we have to decrement the task count and return the pointer
	if (scheduler_get_from_queue(&sched->per_cpu_queue_strict[cpuid], &task, removed)) {
		goto task_obtained;
	}

	if (scheduler_get_from_queue(&sched->per_cpu_queue_preferred[cpuid], &task, removed)) {
		goto task_obtained_preferred;
	}

	if (scheduler_get_from_queue(&sched->per_numa_queue_strict[cpu->numa_node], &task, removed)) {
		goto task_obtained;
	}

	if (scheduler_get_from_queue(&sched->per_numa_queue_preferred[cpu->numa_node], &task, removed)) {
		goto task_obtained_preferred;
	}

	// This function will return the first task without removing it from the queue
	while (scheduler_find_in_queue(&sched->queue, &task)) {
		// Check the task is affine with the current cpu
		if (task_affine(task, cpu)) {
			// Follow normal procedure
			scheduler_get_from_queue(&sched->queue, &task, removed);
			assert(task);
			goto task_obtained;
		}

		// Remove from queue
		scheduler_pop_queue(&sched->queue, task);

		// Not affine. Insert to an appropiate queue
		scheduler_insert_affine(sched, task);
	}

	// If we get here, we didn't find any tasks, otherwise we would've gone to task_obtained
	return NULL;

task_obtained_preferred:
	sched->preferred_affinity_tasks -= *removed;
task_obtained:
	sched->tasks -= *removed;
	return task;
}

// Find tasks by stealing preferred affinity ones
static inline nosv_task_t scheduler_find_task_noaffine_process(process_scheduler_t *sched, cpu_t *cpu, int *removed)
{
	nosv_task_t task = NULL;
	*removed = 1;

	// Are there any tasks?
	if (!sched->tasks)
		return NULL;

	if (sched->preferred_affinity_tasks) {
		int cpus = cpus_count();
		int numas = locality_numa_count();
		// We can try to steal from somewhere
		// Note that we don't skip our own cpus, although we know nothing is there, just for simplicity
		for (int i = 0; i < cpus; ++i) {
			if (scheduler_get_from_queue(&sched->per_cpu_queue_preferred[i], &task, removed))
				goto task_obtained;
		}

		for (int i = 0; i < numas; ++i) {
			if (scheduler_get_from_queue(&sched->per_numa_queue_preferred[i], &task, removed))
				goto task_obtained;
		}
	}

	// Got here, there were no tasks
	return NULL;

task_obtained:
	sched->preferred_affinity_tasks -= *removed;
	sched->tasks -= *removed;

	return task;
}

static inline nosv_task_t scheduler_find_task_yield_process(process_scheduler_t *sched, cpu_t *cpu, __maybe_unused int *removed)
{
	nosv_task_t task;

	// Are there any yield tasks?
	list_head_t *head = list_pop_front(&sched->yield_tasks.tasks);
	if (!head)
		return NULL;

	assert(sched->tasks > 0);

	do {
		task = list_elem(head, struct nosv_task, list_hook);
		task->yield = 0;
		assert(!task_is_parallel(task));

		if (task_affine(task, cpu)) {
			task->scheduled_count++;
			sched->tasks--;
			return task;
		} else {
			scheduler_insert_affine(sched, task);
		}
	} while ((head = list_pop_front(&sched->yield_tasks.tasks)));

	return NULL;
}

static inline nosv_task_t scheduler_get_internal(int cpu)
{
	int pid, yield;
	uint64_t ts;
	list_head_t *it;

	if (!scheduler->tasks) {
		scheduler_update_accounting(0, NULL, cpu, 0);
		return NULL;
	}

	cpu_t *cpu_str = cpu_get(cpu);
	const int external = dtlock_requires_external(&scheduler->dtlock, cpu);

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
		// What we do is grab the current queue, that corresponds to our pid,
		// and traverse that forward, to cause a round-robin.
		// If we get to the end, we start from the beginning.
		// Most importantly, if we arrive at a point where we go back to our queue, it's fine.
		// Otherwise, we will begin from the current position, which means we will try our process first
		it = list_next_circular(it, &scheduler->queues);
	}

	list_head_t *stop = it;
	// Parameter to notify if the task has been removed from the queues or is staying
	// as a parallel task
	int task_removed = 0;

#define SCHEDULER_FOREACH_DO(_function)                                  \
	do {                                                                 \
		sched = list_elem(it, process_scheduler_t, list_hook);           \
		if (!external || sched->pid != pid) {                            \
			nosv_task_t task = _function(sched, cpu_str, &task_removed); \
			if (task) {                                                  \
				scheduler->tasks -= task_removed;                        \
				scheduler->served_tasks += task_removed;                 \
				scheduler_update_accounting(pid, task, cpu, ts);         \
				return task;                                             \
			}                                                            \
		}                                                                \
		it = list_next_circular(it, &scheduler->queues);                 \
	} while (it != stop)

	// Search for a ready task to run. If none is found in the current
	// scheduler, search a ready task in the next scheduler in the list.
	SCHEDULER_FOREACH_DO(scheduler_find_task_process);

	// If we didn't find any affine or "normal" tasks to execute, we can search now
	// for the "next best thing", which is stealing affine tasks
	SCHEDULER_FOREACH_DO(scheduler_find_task_noaffine_process);

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

	SCHEDULER_FOREACH_DO(scheduler_find_task_yield_process);

#undef SCHEDULER_FOREACH_DO

	// This may only happen if there are strict bindings that we couldn't steal
	return NULL;
}

static inline void scheduler_serve(nosv_task_t task, uint32_t scheduled_count, int cpu)
{
	int waiter = governor_served(&scheduler->governor, cpu);
	int action = (waiter) ? DTLOCK_SIGNAL_WAKE : DTLOCK_SIGNAL_DEFAULT;

	dtlock_serve(&scheduler->dtlock, cpu, task, scheduled_count, action);

	instr_sched_send();
}

static inline size_t scheduler_serve_batch(int *cpus_were_skipped, cpu_bitset_t *cpus_to_serve)
{
	// Serve a task batch
	// TODO instead of call scheduler_get_internal in a loop,
	// do something smarter
	size_t served = 0;
	int cpu_delegated = 0;

	CPU_BITSET_FOREACH(cpus_to_serve, cpu_delegated)
	{
		assert(cpu_delegated < cpus_count());
		nosv_task_t task = scheduler_get_internal(cpu_delegated);

		// If we don't get a task, indicate this situation to the server thread
		// Additionally, don't wake the other thread up
		if (!task) {
			*cpus_were_skipped = 1;
		} else {
			scheduler_serve(task, task->scheduled_count, cpu_delegated);
			served++;
		}
	}

	return served;
}

task_execution_handle_t scheduler_get(int cpu, nosv_flags_t flags)
{
	assert(cpu >= 0);

	// Whether the thread can block serving tasks
	const int blocking = !(flags & SCHED_GET_NONBLOCKING);
	const int external = !!(flags & SCHED_GET_EXTERNAL);
	task_execution_handle_t handle = EMPTY_TASK_EXECUTION_HANDLE;

	if (!dtlock_lock_or_delegate(&scheduler->dtlock, (uint64_t) cpu, (void **) &handle.task, &handle.execution_id, blocking, external)) {
		// Served item
		if (handle.task) {
			instr_worker_progressing();
			instr_sched_recv();
		} else if (!blocking) {
			// Otherwise we would mark non-blocking gets as idle all the time, which is not correct
			instr_worker_progressing();
		}

		assert(handle.task == NULL || handle.execution_id != 0);
		return handle;
	}

	// Lock acquired
	instr_worker_progressing();
	instr_sched_server_enter();

	cpu_bitset_t *waiters = governor_get_waiters(&scheduler->governor);
	cpu_bitset_t *sleepers = governor_get_sleepers(&scheduler->governor);

	do {
		scheduler_process_ready_tasks(1);

		scheduler_deadline_purge();

		size_t served = 0;

		int pending = governor_update_cpumasks(&scheduler->governor, &scheduler->dtlock);

		// Serve everyone waiting
		// As soon as we cannot schedule one CPU, stop the loop and try to schedule ourselves
		// This is needed because otherwise strict affinity tasks may have problems
		int skip = 0;
		while (served < MAX_SERVED_TASKS && pending && !skip) {
			size_t served_current = 0;

			// First, schedule waiters
			served_current += scheduler_serve_batch(&skip, waiters);
			// Then, sleepers
			served_current += scheduler_serve_batch(&skip, sleepers);

			// Apply some powersaving policies to all threads kept waiting
			// Do this before reading the newly arrived cores, otherwise all non-blocking
			// waiters would be released immediately without a chance to get a scheduled task
			governor_apply_policy(&scheduler->governor, &scheduler->dtlock);

			// Process any newly arrived cores
			pending = governor_update_cpumasks(&scheduler->governor, &scheduler->dtlock);

			if (served_current)
				instr_worker_progressing();
			else
				instr_worker_resting();

			served += served_current;
		}

		// Work for myself
		handle.task = scheduler_get_internal(cpu);
	} while (!handle.task && blocking && !worker_should_shutdown());

	// Record execution count when serving myself
	if (handle.task)
		handle.execution_id = handle.task->scheduled_count;

	instr_worker_progressing();

	// Keep one thread inside the lock
	if (dtlock_empty(&scheduler->dtlock))
		governor_wake_one(&scheduler->governor, &scheduler->dtlock);

	dtlock_unlock(&scheduler->dtlock);

	if (handle.task)
		instr_sched_self_assign();

	assert(handle.task == NULL || handle.execution_id != 0);

	instr_sched_server_exit();

	return handle;
}
