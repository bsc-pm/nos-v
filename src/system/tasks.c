/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include "nosv-internal.h"
#include "generic/clock.h"
#include "hardware/cpus.h"
#include "hardware/threads.h"
#include "hardware/pids.h"
#include "memory/slab.h"
#include "scheduler/scheduler.h"
#include "system/tasks.h"

#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>

#define LABEL_MAX_CHAR 128

/* Create a task type with certain run/end callbacks and a label */
int nosv_type_init(
	nosv_task_type_t *type /* out */,
	nosv_task_run_callback_t run_callback,
	nosv_task_end_callback_t end_callback,
	nosv_task_completed_callback_t completed_callback,
	const char *label,
	void *metadata,
	nosv_flags_t flags)
{
	if (unlikely(!type))
		return -EINVAL;

	if (unlikely(!run_callback && !(flags & NOSV_TYPE_INIT_EXTERNAL)))
		return -EINVAL;

	nosv_task_type_t res = salloc(sizeof(struct nosv_task_type), cpu_get_current());

	if (!res)
		return -ENOMEM;

	res->run_callback = run_callback;
	res->end_callback = end_callback;
	res->completed_callback = completed_callback;
	res->metadata = metadata;
	res->pid = logical_pid;

	if (label) {
		res->label = strndup(label, LABEL_MAX_CHAR - 1);
		assert(res->label);
	} else {
		res->label = NULL;
	}

	*type = res;
	return 0;
}

/* Getters and setters */
nosv_task_run_callback_t nosv_get_task_type_run_callback(nosv_task_type_t type)
{
	return type->run_callback;
}

nosv_task_end_callback_t nosv_get_task_type_end_callback(nosv_task_type_t type)
{
	return type->end_callback;
}

nosv_task_completed_callback_t nosv_get_task_type_completed_callback(nosv_task_type_t type)
{
	return type->completed_callback;
}

const char *nosv_get_task_type_label(nosv_task_type_t type)
{
	return type->label;
}

// TODO Maybe it's worth it to place the metadata at the top to prevent continuous calls to nOS-V
void *nosv_get_task_type_metadata(nosv_task_type_t type)
{
	return type->metadata;
}

int nosv_type_destroy(
	nosv_task_type_t type,
	nosv_flags_t flags)
{
	if (type->label)
		free((void *) type->label);

	sfree(type, sizeof(struct nosv_task_type), cpu_get_current());
	return 0;
}

static inline int nosv_create_internal(nosv_task_t *task /* out */,
	nosv_task_type_t type,
	size_t metadata_size,
	nosv_flags_t flags)
{
	nosv_task_t res = salloc(sizeof(struct nosv_task) + metadata_size, cpu_get_current());

	if (!res)
		return -ENOMEM;

	res->type = type;
	res->metadata = metadata_size;
	res->worker = NULL;
	atomic_init(&res->event_count, 1);
	atomic_init(&res->blocking_count, 1);
	res->affinity.type = 0;
	res->affinity.index = 0;
	res->affinity.level = 0;
	res->deadline = 0;
	res->yield = 0;
	res->wakeup = NULL;

	*task = res;

	return 0;
}

/* May return -ENOMEM. 0 on success */
/* Callable from everywhere */
int nosv_create(
	nosv_task_t *task /* out */,
	nosv_task_type_t type,
	size_t metadata_size,
	nosv_flags_t flags)
{
	if (unlikely(!task))
		return -EINVAL;

	if (unlikely(!type))
		return -EINVAL;

	if (unlikely(metadata_size > NOSV_MAX_METADATA_SIZE))
		return -EINVAL;

	return nosv_create_internal(task, type, metadata_size, flags);
}

/* Getters and setters */
/* Read-only task attributes */
void *nosv_get_task_metadata(nosv_task_t task)
{
	return ((char *)task) + sizeof(struct nosv_task);
}

nosv_task_type_t nosv_get_task_type(nosv_task_t task)
{
	return task->type;
}

/* Read-write task attributes */
int nosv_get_task_priority(nosv_task_t task)
{
	return task->priority;
}

void nosv_set_task_priority(nosv_task_t task, int priority)
{
	task->priority = priority;
}

/* Callable from everywhere */
int nosv_submit(
	nosv_task_t task,
	nosv_flags_t flags)
{
	if (unlikely(!task))
		return -EINVAL;

	if (flags & NOSV_SUBMIT_BLOCKING) {
		// For now we don't support blocking submits outside a task context
		if (!worker_is_in_task())
			return -EINVAL;

		// Not compatible
		if (unlikely(flags & NOSV_SUBMIT_IMMEDIATE))
			return -EINVAL;
		if (unlikely(flags & NOSV_SUBMIT_INLINE))
			return -EINVAL;

		task->wakeup = worker_current_task();
	}

	uint32_t count;

	// If we have an immediate successor we don't place the task into the scheduler
	if (flags & NOSV_SUBMIT_IMMEDIATE) {
		// Must be from a worker
		if (!worker_is_in_task())
			return -EINVAL;

		// Not compatible
		if (unlikely(flags & NOSV_SUBMIT_INLINE))
			return -EINVAL;

		if (worker_get_immediate()) {
			// Setting a new immediate successor, but there was already one.
			// Place the new one and send the old one to the scheduler

			scheduler_submit(worker_get_immediate());
		}

		worker_set_immediate(task);

		count = atomic_fetch_sub_explicit(&task->blocking_count, 1, memory_order_relaxed) - 1;
		assert(count == 0);
	} else if (flags & NOSV_SUBMIT_INLINE) {
		nosv_worker_t *worker = worker_current();
		// We cannot execute tasks without a valid worker
		if (unlikely(!worker))
			return -EINVAL;

		count = atomic_fetch_sub_explicit(&task->blocking_count, 1, memory_order_relaxed) - 1;
		assert(count == 0);

		nosv_task_t old_task = worker_current_task();
		assert(old_task);

		// Assign and execute the new task
		task->worker = worker;
		worker->task = task;
		task_execute(task);

		// Restore old task
		worker->task = old_task;
	} else {
		count = atomic_fetch_sub_explicit(&task->blocking_count, 1, memory_order_relaxed) - 1;

		if (count == 0)
			scheduler_submit(task);
	}

	if (flags & NOSV_SUBMIT_BLOCKING)
		nosv_pause(NOSV_PAUSE_NONE);

	return 0;
}

/* Blocking, yield operation */
/* Callable from a task context ONLY */
int nosv_pause(
	nosv_flags_t flags)
{
	// We have to be inside a worker
	if (!worker_is_in_task())
		return -EINVAL;

	nosv_task_t task = worker_current_task();
	assert(task);

	uint32_t count = atomic_fetch_add_explicit(&task->blocking_count, 1, memory_order_relaxed) + 1;

	// If r < 1, we have already been unblocked
	if (count > 0)
		worker_yield();

	return 0;
}

/* Deadline tasks */
int nosv_waitfor(
	uint64_t ns)
{
	// We have to be inside a worker
	if (!worker_is_in_task())
		return -EINVAL;

	nosv_task_t task = worker_current_task();
	assert(task);

	task->deadline = clock_ns() + ns;

	// Submit the task to re-schedule when the deadline is done
	scheduler_submit(task);

	// Block until the deadline expires
	worker_yield();

	// Unblocked
	task->deadline = 0;

	return 0;
}

/* Yield operation */
/* Restriction: Can only be called from a task context */
int nosv_yield(
	nosv_flags_t flags)
{
	if (!worker_is_in_task())
		return -EINVAL;

	nosv_task_t task = worker_current_task();
	task->yield = -1;
	scheduler_submit(task);
	worker_yield();

	return 0;
}

/* Callable from everywhere */
int nosv_destroy(
	nosv_task_t task,
	nosv_flags_t flags)
{
	if (unlikely(!task))
		return -EINVAL;

	sfree(task, sizeof(struct nosv_task) + task->metadata, cpu_get_current());

	return 0;
}

static inline void task_complete(nosv_task_t task)
{
	nosv_task_t wakeup = task->wakeup;
	task->wakeup = NULL;

	if (task->type->completed_callback)
		task->type->completed_callback(task);
	// From here, task may be freed!

	if (wakeup)
		nosv_submit(wakeup, NOSV_SUBMIT_UNLOCKED);
}

void task_execute(nosv_task_t task)
{
	atomic_thread_fence(memory_order_acquire);
	task->type->run_callback(task);
	atomic_thread_fence(memory_order_release);

	if (task->type->end_callback) {
		atomic_thread_fence(memory_order_acquire);
		task->type->end_callback(task);
		atomic_thread_fence(memory_order_release);
	}

	uint64_t res = atomic_fetch_sub_explicit(&task->event_count, 1, memory_order_relaxed) - 1;
	if (!res) {
		task_complete(task);
	}
}

/* Events API */
/* Restriction: Can only be called from a task context */
int nosv_increase_event_counter(
	uint64_t increment)
{
	if (!increment)
		return -EINVAL;

	nosv_task_t current = worker_current_task();

	if (!current)
		return -EINVAL;

	atomic_fetch_add_explicit(&current->event_count, increment, memory_order_relaxed);

	return 0;
}

/* Restriction: Can only be called from a nOS-V Worker */
int nosv_decrease_event_counter(
	nosv_task_t task,
	uint64_t decrement)
{
	if (!task)
		return -EINVAL;

	if (!decrement)
		return -EINVAL;

	uint64_t r = atomic_fetch_sub_explicit(&task->event_count, decrement, memory_order_relaxed) - 1;

	if (!r) {
		task_complete(task);
	}

	return 0;
}

/*
	Attach "adopts" an external thread. We have to create a nosv_worker to represent this thread,
	and create an implicit task. Note that the task's callbacks will not be called, and we will
	consider it an error.
	The task will be placed with an attached worker into the scheduler, and the worker will be blocked.
*/
int nosv_attach(
	nosv_task_t *task /* out */,
	nosv_task_type_t type /* must have null callbacks */,
	size_t metadata_size,
	nosv_affinity_t *affinity,
	nosv_flags_t flags)
{
	if (unlikely(!task))
		return -EINVAL;

	if (unlikely(!type))
		return -EINVAL;

	if (unlikely(metadata_size > NOSV_MAX_METADATA_SIZE))
		return -EINVAL;

	if (unlikely(type->run_callback || type->end_callback || type->completed_callback))
		return -EINVAL;

	nosv_worker_t *worker = worker_create_external(pthread_self());
	assert(worker);

	int ret = nosv_create_internal(task, type, metadata_size, flags);

	if (ret) {
		worker_free_external(worker);
		return ret;
	}

	// We created the task fine. Now map the task to the worker
	nosv_task_t t = *task;
	t->worker = worker;
	worker->task = t;

	// Set the affinity if required
	if (affinity != NULL) {
		t->affinity = *affinity;
	}

	atomic_fetch_sub_explicit(&t->blocking_count, 1, memory_order_relaxed);

	// Submit task for scheduling at an actual CPU
	scheduler_submit(t);

	// Block the worker
	worker_block();
	// Now we have been scheduled, return

	return 0;
}

/*
	Detach removes the external thread. We must free the associated worker and task,
	and restore a different worker in the current CPU.
*/
int nosv_detach(
	nosv_flags_t flags)
{
	// First, make sure we are on a worker context
	nosv_worker_t *worker = worker_current();

	if (!worker)
		return -EINVAL;

	if (!worker->task)
		return -EINVAL;

	// First free the task
	nosv_destroy(worker->task, NOSV_DESTROY_NONE);

	cpu_t *cpu = worker->cpu;
	assert(cpu);

	// Restore the worker's affinity to its original value
	// Optionally deactivated with the NOSV_DETACH_NO_RESTORE_AFFINITY flag
	if (!(flags & NOSV_DETACH_NO_RESTORE_AFFINITY))
		sched_setaffinity(0, sizeof(worker->original_affinity), &worker->original_affinity);

	// Now free the worker
	// We have to free before waking up another worker on the current CPU
	// Otherwise the sfree inside worker_free_external does not have exclusive access to the cpu buckets
	worker_free_external(worker);

	// Then resume a thread on the current cpu
	worker_wake(logical_pid, cpu, NULL);

	return 0;
}

nosv_affinity_t nosv_get_task_affinity(nosv_task_t task)
{
	return task->affinity;
}

void nosv_set_task_affinity(nosv_task_t task, nosv_affinity_t *affinity)
{
	assert(affinity != NULL);

	task->affinity = *affinity;
}

nosv_task_t nosv_self(void)
{
	return worker_current_task();
}
