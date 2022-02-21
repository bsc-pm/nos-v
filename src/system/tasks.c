/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include "generic/clock.h"
#include "hardware/cpus.h"
#include "hardware/pids.h"
#include "hardware/threads.h"
#include "hwcounters/hwcounters.h"
#include "instr.h"
#include "memory/slab.h"
#include "monitoring/monitoring.h"
#include "nosv-internal.h"
#include "scheduler/scheduler.h"
#include "system/tasks.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>

__internal task_type_manager_t *task_type_manager;

// Start the taskid and typeid counters at 1 so we have the same
// identifiers in Paraver. It is also used to check for overflows.
static atomic_uint64_t taskid_counter = 1;
static atomic_uint32_t typeid_counter = 1;

#define LABEL_MAX_CHAR 128

//! \brief Initialize the manager of task types
void task_type_manager_init()
{
	task_type_manager = (task_type_manager_t *) malloc(sizeof(task_type_manager_t));
	assert(task_type_manager != NULL);

	// Initialize the list of tasktypes and the spinlock
	list_init(&task_type_manager->types);
	nosv_spin_init(&task_type_manager->lock);
}

list_head_t *task_type_manager_get_list()
{
	return &(task_type_manager->types);
}

//! \brief Shutdown the manager of task types
void task_type_manager_shutdown()
{
	// Destroy the spinlock and the manager
	nosv_spin_destroy(&task_type_manager->lock);

	list_head_t *list = task_type_manager_get_list();
	list_head_t *head = list_front(list);
	list_head_t *stop = head;
	do {
		nosv_task_type_t type = list_elem(head, struct nosv_task_type, list_hook);
		if (type != NULL) {
			if (type->label)
				free((void *)type->label);

			sfree(type, sizeof(struct nosv_task_type) + monitoring_get_tasktype_size(), cpu_get_current());
		}

		head = list_next_circular(head, list);
	} while (head != stop);

	free(task_type_manager);
}

/* Create a task type with certain run/end callbacks and a label */
int nosv_type_init(
	nosv_task_type_t *type /* out */,
	nosv_task_run_callback_t run_callback,
	nosv_task_end_callback_t end_callback,
	nosv_task_completed_callback_t completed_callback,
	const char *label,
	void *metadata,
	nosv_cost_function_t cost_function,
	nosv_flags_t flags)
{
	if (unlikely(!type))
		return -EINVAL;

	if (unlikely(!run_callback && !(flags & NOSV_TYPE_INIT_EXTERNAL)))
		return -EINVAL;

	nosv_task_type_t res = salloc(
		sizeof(struct nosv_task_type) +
		monitoring_get_tasktype_size(),
		cpu_get_current()
	);

	if (!res)
		return -ENOMEM;

	res->run_callback = run_callback;
	res->end_callback = end_callback;
	res->completed_callback = completed_callback;
	res->metadata = metadata;
	res->pid = logic_pid;
	res->typeid = atomic_fetch_add_explicit(&typeid_counter, 1, memory_order_relaxed);
	res->get_cost = cost_function;

	// Monitoring type statistics are right after the type's memory
	res->stats = (tasktype_stats_t *) (((char *) res) + sizeof(struct nosv_task_type));

	if (label) {
		res->label = strndup(label, LABEL_MAX_CHAR - 1);
		assert(res->label);
	} else {
		res->label = NULL;
	}

	instr_type_create(res->typeid, res->label);

	// Add the task type to the list of registered task types
	nosv_spin_lock(&task_type_manager->lock);
	list_add_tail(&task_type_manager->types, &(res->list_hook));
	nosv_spin_unlock(&task_type_manager->lock);

	// Entry point - A type has just been created
	monitoring_type_created(res);

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
	// Empty, used for API completeness. Types are destroyed in
	// task_type_manager_shutdown
	return 0;
}

static inline int nosv_create_internal(nosv_task_t *task /* out */,
	nosv_task_type_t type,
	size_t metadata_size,
	nosv_flags_t flags)
{
	nosv_task_t res = salloc(
		sizeof(struct nosv_task) +   /* Size of the struct itself */
		metadata_size +              /* The size needed to allocate the task's metadata */
		hwcounters_get_task_size() + /* Size needed to allocate hardware counter for the task */
		monitoring_get_task_size(),  /* Size needed to allocate monitoring stats for the task */
		cpu_get_current()
	);

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
	res->taskid = atomic_fetch_add_explicit(&taskid_counter, 1, memory_order_relaxed);
	res->counters = (task_hwcounters_t *) (((char *) res) + sizeof(struct nosv_task) + metadata_size);
	res->stats = (task_stats_t *) (((char *) res) + sizeof(struct nosv_task) + metadata_size + hwcounters_get_task_size());

	// Initialize hardware counters and monitoring for the task
	hwcounters_task_created(res, /* enabled */ 1);
	monitoring_task_created(res);

	*task = res;

	instr_task_create((uint32_t)res->taskid, res->type->typeid);

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

	// Update the counters of the current task if it exists, as we don't want
	// the creation to be accounted in this task's counters
	nosv_task_t current_task = worker_current_task();
	if (current_task) {
		hwcounters_update_task_counters(current_task);
		monitoring_task_changed_status(current_task, paused_status);
	}

	int ret = nosv_create_internal(task, type, metadata_size, flags);

	if (current_task) {
		hwcounters_update_runtime_counters();
		monitoring_task_changed_status(current_task, executing_status);
	}

	return ret;
}

/* Getters and setters */
/* Read-only task attributes */
void *nosv_get_task_metadata(nosv_task_t task)
{
	// Check if any metadata was allocated
	if (task->metadata)
		return ((char *) task) + sizeof(struct nosv_task);

	return NULL;
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

	const bool is_blocking = (flags & NOSV_SUBMIT_BLOCKING);
	const bool is_immediate = (flags & NOSV_SUBMIT_IMMEDIATE);
	const bool is_inline = (flags & NOSV_SUBMIT_INLINE);

	// These submit modes are mutually exclusive
	if (unlikely(is_immediate + is_blocking + is_inline > 1))
		return -EINVAL;

	if (is_blocking || is_inline) {
		// These submit modes cannot be used from outside a task context
		if (!worker_is_in_task())
			return -EINVAL;
	}

	// If we're in a task context, update task counters now since we don't want
	// the creation to be added to the counters of the task
	nosv_task_t current_task = worker_current_task();
	if (current_task) {
		hwcounters_update_task_counters(current_task);
		monitoring_task_changed_status(current_task, paused_status);
	}

	instr_submit_enter();
	monitoring_task_submitted(task);

	nosv_worker_t *worker = worker_current();
	if (is_blocking)
		task->wakeup = worker->task;

	uint32_t count;

	// Entry point - The task became ready
	monitoring_task_changed_status(task, ready_status);

	// If we have an immediate successor we don't place the task into the scheduler
	// However, if we're not in a worker context, or we are currently executing a task,
	// we will ignore the request, as it would hang the program.
	if (is_immediate && worker && !worker->in_task_body) {
		if (worker_get_immediate()) {
			// Setting a new immediate successor, but there was already one.
			// Place the new one and send the old one to the scheduler

			scheduler_submit(worker_get_immediate());
		}

		count = atomic_fetch_sub_explicit(&task->blocking_count, 1, memory_order_relaxed) - 1;
		assert(count == 0);

		worker_set_immediate(task);
	} else if (is_inline) {
		nosv_worker_t *worker = worker_current();
		assert(worker);

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

	if (is_blocking)
		nosv_pause(NOSV_PAUSE_NONE);

	if (current_task) {
		hwcounters_update_runtime_counters();
		monitoring_task_changed_status(current_task, executing_status);
	}

	instr_submit_exit();

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

	// Thread might yield, read and accumulate hardware counters for the task that blocks
	hwcounters_update_task_counters(task);
	monitoring_task_changed_status(task, paused_status);

	instr_task_pause((uint32_t)task->taskid);
	instr_pause_enter();

	uint32_t count = atomic_fetch_add_explicit(&task->blocking_count, 1, memory_order_relaxed) + 1;

	// If r < 1, we have already been unblocked
	if (count > 0)
		worker_yield();

	// Thread might have been resumed here, read and accumulate hardware counters for the CPU
	hwcounters_update_runtime_counters();
	monitoring_task_changed_status(task, executing_status);

	instr_pause_exit();
	instr_task_resume((uint32_t)task->taskid);

	return 0;
}

/* Deadline tasks */
int nosv_waitfor(
	uint64_t target_ns,
	uint64_t *actual_ns /* out */)
{
	// We have to be inside a worker
	if (!worker_is_in_task())
		return -EINVAL;

	nosv_task_t task = worker_current_task();
	assert(task);

	// Thread is gonna yield, read and accumulate hardware counters for the task
	hwcounters_update_task_counters(task);
	monitoring_task_changed_status(task, ready_status);

	instr_task_pause((uint32_t)task->taskid);
	instr_waitfor_enter();

	const uint64_t start_ns = clock_ns();
	task->deadline = start_ns + target_ns;

	// Submit the task to re-schedule when the deadline is done
	scheduler_submit(task);

	// Block until the deadline expires
	worker_yield();

	// Unblocked
	task->deadline = 0;

	if (actual_ns)
		*actual_ns = clock_ns() - start_ns;

	// Thread has been resumed, read and accumulate hardware counters for the CPU
	hwcounters_update_runtime_counters();
	monitoring_task_changed_status(task, executing_status);

	instr_waitfor_exit();
	instr_task_resume((uint32_t)task->taskid);

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
	assert(task);

	// Thread is gonna yield, read and accumulate hardware counters for the task
	hwcounters_update_task_counters(task);
	monitoring_task_changed_status(task, ready_status);

	instr_task_pause((uint32_t)task->taskid);
	instr_yield_enter();

	// Mark the task as yield
	task->yield = -1;

	// Yield the CPU if there is available work in the scheduler
	worker_yield_if_needed(task);

	// Unmark the task as yield
	task->yield = 0;

	// Thread has been resumed, read and accumulate hardware counters for the CPU
	hwcounters_update_runtime_counters();
	monitoring_task_changed_status(task, executing_status);

	instr_yield_exit();
	instr_task_resume((uint32_t)task->taskid);

	return 0;
}

int nosv_schedpoint(
	nosv_flags_t flags)
{
	int pid, yield, cpuid;
	uint64_t ts;

	if (!worker_is_in_task())
		return -EINVAL;

	nosv_task_t task = worker_current_task();
	assert(task);

	// Thread is gonna yield, read and accumulate hardware counters for the task
	hwcounters_update_task_counters(task);
	monitoring_task_changed_status(task, ready_status);

	instr_task_pause((uint32_t)task->taskid);
	instr_schedpoint_enter();

	cpuid = cpu_get_current();
	pid = cpu_get_pid(cpuid);

	// Check if the current PID's quantum is exceeded
	yield = scheduler_should_yield(pid, cpuid, &ts);

	if (yield) {
		// Yield the CPU if there is available work in the scheduler
		worker_yield_if_needed(task);

		cpuid = cpu_get_current();

		// Give a new quantum to the process because it may have returned
		// to this same task directly if there are no other ready tasks
		scheduler_reset_accounting(pid, cpuid);
	}

	// Thread has been resumed, read and accumulate hardware counters for the CPU
	hwcounters_update_runtime_counters();
	monitoring_task_changed_status(task, executing_status);

	instr_schedpoint_exit();
	instr_task_resume((uint32_t)task->taskid);

	return 0;
}

/* Callable from everywhere */
int nosv_destroy(
	nosv_task_t task,
	nosv_flags_t flags)
{
	if (unlikely(!task))
		return -EINVAL;

	sfree(task, sizeof(struct nosv_task) +
		task->metadata +
		hwcounters_get_task_size() +
		monitoring_get_task_size(),
		cpu_get_current()
	);

	return 0;
}

static inline void task_complete(nosv_task_t task)
{
	// Entry point - A task has just completed its execution
	monitoring_task_completed(task);

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
	// Task is about to execute, update runtime counters
	hwcounters_update_runtime_counters();
	monitoring_task_changed_status(task, executing_status);

	const uint32_t taskid = (uint32_t) task->taskid;
	instr_task_execute(taskid);

	nosv_worker_t *worker = worker_current();
	assert(worker);

	worker->in_task_body = 1;

	atomic_thread_fence(memory_order_acquire);
	task->type->run_callback(task);
	atomic_thread_fence(memory_order_release);

	worker->in_task_body = 0;

	if (task->type->end_callback) {
		atomic_thread_fence(memory_order_acquire);
		task->type->end_callback(task);
		atomic_thread_fence(memory_order_release);
	}

	// Task just completed, read and accumulate hardware counters for the task
	hwcounters_update_task_counters(task);
	monitoring_task_changed_status(task, paused_status);

	uint64_t res = atomic_fetch_sub_explicit(&task->event_count, 1, memory_order_relaxed) - 1;
	if (!res) {
		task_complete(task);
	}
	// Warning: from this point forward, "task" may have been freed, and thus it is not safe to access

	instr_task_end(taskid);
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

	// If a task is in here, make sure this is not accounted as executing
	nosv_task_t current = worker_current_task();
	if (current) {
		hwcounters_update_task_counters(current);
		monitoring_task_changed_status(current, paused_status);
	}

	uint64_t r = atomic_fetch_sub_explicit(&task->event_count, decrement, memory_order_relaxed) - 1;

	if (!r) {
		task_complete(task);
	}

	if (current) {
		hwcounters_update_runtime_counters();
		monitoring_task_changed_status(current, executing_status);
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

	instr_thread_attach();

	nosv_worker_t *worker = worker_create_external();
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

	// Entry point - A task becomes ready
	monitoring_task_changed_status(t, ready_status);

	// Submit task for scheduling at an actual CPU
	scheduler_submit(t);

	// Block the worker
	worker_block();
	// Now we have been scheduled, return

	// Task is about to execute, update runtime counters
	hwcounters_update_runtime_counters();
	monitoring_task_changed_status(t, executing_status);

	// Inform the instrumentation about the new task being in
	// execution, as it won't pass via task_execute()
	instr_task_execute((uint32_t)t->taskid);

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

	// Task just completed, read and accumulate hardware counters for the task
	hwcounters_update_task_counters(worker->task);
	monitoring_task_completed(worker->task);

	instr_task_end((uint32_t)worker->task->taskid);

	// First free the task
	nosv_destroy(worker->task, NOSV_DESTROY_NONE);

	cpu_t *cpu = worker->cpu;
	assert(cpu);

	// Restore the worker's affinity to its original value
	// Optionally deactivated with the NOSV_DETACH_NO_RESTORE_AFFINITY flag
	if (!(flags & NOSV_DETACH_NO_RESTORE_AFFINITY)) {
		// This thread now goes to an unknown CPU
		instr_affinity_set(-1);
		sched_setaffinity(0, sizeof(worker->original_affinity), &worker->original_affinity);
	}

	// Now free the worker
	// We have to free before waking up another worker on the current CPU
	// Otherwise the sfree inside worker_free_external does not have exclusive access to the cpu buckets
	worker_free_external(worker);

	// Then resume a thread on the current cpu
	worker_wake_idle(logic_pid, cpu, NULL);

	instr_thread_detach();

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
