/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#include "generic/clock.h"
#include "generic/cpuset.h"
#include "generic/list.h"
#include "hardware/cpus.h"
#include "hardware/pids.h"
#include "hwcounters/hwcounters.h"
#include "instr.h"
#include "memory/slab.h"
#include "monitoring/monitoring.h"
#include "nosv-internal.h"
#include "scheduler/scheduler.h"
#include "support/affinity.h"
#include "system/tasks.h"
#include "system/taskgroup.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>

__internal task_type_manager_t *task_type_manager;
__internal thread_local int32_t rt_attach_refcount = 0;

// Start the taskid and typeid counters at 1 so we have the same
// identifiers in Paraver. It is also used to check for overflows.
static atomic_uint64_t taskid_counter = 1;
static atomic_uint32_t typeid_counter = 1;

extern __internal thread_local struct kinstr *kinstr;

static nosv_affinity_t default_affinity;

#define LABEL_MAX_CHAR 128

//! \brief Initialize the manager of task types
void task_type_manager_init(void)
{
	task_type_manager = (task_type_manager_t *) malloc(sizeof(task_type_manager_t));
	assert(task_type_manager != NULL);

	// Initialize the list of tasktypes and the spinlock
	list_init(&task_type_manager->types);
	nosv_spin_init(&task_type_manager->lock);
}

static inline nosv_affinity_t parse_affinity_from_config(void)
{
	nosv_affinity_t ret;

	// Start by splitting the prefix and the index
	char *sep = strchr(nosv_config.affinity_default, '-');

	// Check if sep is NULL (not found)
	if (!sep)
		nosv_abort("Malformed default_affinity string");

	char *index = sep + 1;
	// NULL-terminate the string to parse it
	*sep = '\0';

	if (!strcmp(nosv_config.affinity_default, "cpu"))
		ret.level = NOSV_AFFINITY_LEVEL_CPU;
	else if (!strcmp(nosv_config.affinity_default, "numa"))
		ret.level = NOSV_AFFINITY_LEVEL_NUMA;
	else
		nosv_abort("Unknown default affinity level");

	errno = 0;
	unsigned long parsed_index = strtoul(index, NULL, 10);

	if (errno)
		nosv_abort("Invalid default affinity index");

	ret.index = parsed_index;

	if (!strcmp(nosv_config.affinity_default_policy, "strict"))
		ret.type = NOSV_AFFINITY_TYPE_STRICT;
	else
		ret.type = NOSV_AFFINITY_TYPE_PREFERRED;

	return ret;
}

// Initialize the task default affinity parsing the configuration
void task_affinity_init(void)
{
	if (!strcmp(nosv_config.affinity_default, "all")) {
		default_affinity.index = default_affinity.level = default_affinity.type = 0;
	} else {
		default_affinity = parse_affinity_from_config();
	}
}

list_head_t *task_type_manager_get_list(void)
{
	return &(task_type_manager->types);
}

//! \brief Shutdown the manager of task types
void task_type_manager_shutdown(void)
{
	// Destroy the spinlock and the manager
	nosv_spin_destroy(&task_type_manager->lock);

	list_head_t *list = task_type_manager_get_list();
	list_head_t *head;
	list_for_each(head, list) {
		nosv_task_type_t type = list_elem(head, struct nosv_task_type, list_hook);
		if (type != NULL) {
			if (type->label)
				free((void *)type->label);

			sfree(type, sizeof(struct nosv_task_type) + monitoring_get_tasktype_size(), cpu_get_current());
		}
	}

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
		return NOSV_ERR_INVALID_PARAMETER;

	if (unlikely(!run_callback && !(flags & NOSV_TYPE_INIT_EXTERNAL)))
		return NOSV_ERR_INVALID_CALLBACK;

	nosv_task_type_t res = salloc(
		sizeof(struct nosv_task_type) +
		monitoring_get_tasktype_size(),
		cpu_get_current()
	);

	if (!res)
		return NOSV_ERR_OUT_OF_MEMORY;

	res->run_callback = run_callback;
	res->end_callback = end_callback;
	res->completed_callback = completed_callback;
	res->metadata = metadata;
	res->pid = logic_pid;
	res->typeid = atomic_fetch_add_explicit(&typeid_counter, 1, memory_order_relaxed);
	res->get_cost = cost_function;
	list_init(&(res->list_hook));

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

	return NOSV_SUCCESS;
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
	return NOSV_SUCCESS;
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
		return NOSV_ERR_OUT_OF_MEMORY;

	res->type = type;
	res->metadata = metadata_size;
	res->worker = NULL;
	atomic_init(&res->event_count, 1);
	atomic_init(&res->blocking_count, 1);
	res->affinity = default_affinity;
	res->priority = 0;
	list_init(&res->list_hook);

	res->deadline = 0;
	atomic_init(&res->deadline_state, NOSV_DEADLINE_NONE);
	res->yield = 0;
	res->wakeup = NULL;
	res->taskid = atomic_fetch_add_explicit(&taskid_counter, 1, memory_order_relaxed);
	res->counters = (task_hwcounters_t *) (((char *) res) + sizeof(struct nosv_task) + metadata_size);
	res->stats = (task_stats_t *) (((char *) res) + sizeof(struct nosv_task) + metadata_size + hwcounters_get_task_size());

	atomic_store_explicit(&(res->degree), 1, memory_order_relaxed);
	res->scheduled_count = 0;
	res->flags = flags;

	task_group_init(&res->submit_window);
	res->submit_window_maxsize = 1;

	// Initialize hardware counters and monitoring for the task
	hwcounters_task_created(res, /* enabled */ 1);
	monitoring_task_created(res);

	*task = res;

	if (flags & NOSV_CREATE_PARALLEL)
		instr_task_create_par((uint32_t)res->taskid, res->type->typeid);
	else
		instr_task_create((uint32_t)res->taskid, res->type->typeid);

	return NOSV_SUCCESS;
}

/* May return NOSV_ERR_OUT_OF_MEMORY. 0 on success */
/* Callable from everywhere */
int nosv_create(
	nosv_task_t *task /* out */,
	nosv_task_type_t type,
	size_t metadata_size,
	nosv_flags_t flags)
{
	if (unlikely(!task))
		return NOSV_ERR_INVALID_PARAMETER;

	if (unlikely(!type))
		return NOSV_ERR_INVALID_PARAMETER;

	if (unlikely(metadata_size > NOSV_MAX_METADATA_SIZE))
		return NOSV_ERR_INVALID_METADATA_SIZE;

	instr_create_enter();

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

	instr_create_exit();

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
		return NOSV_ERR_INVALID_PARAMETER;

	const bool is_blocking = (flags & NOSV_SUBMIT_BLOCKING);
	const bool is_immediate = (flags & NOSV_SUBMIT_IMMEDIATE) && nosv_config.sched_immediate_successor;
	const bool is_inline = (flags & NOSV_SUBMIT_INLINE);
	const bool is_dl_wake = (flags & NOSV_SUBMIT_DEADLINE_WAKE);

	// These submit modes are mutually exclusive
	if (unlikely(is_immediate + is_blocking + is_inline + is_dl_wake > 1))
		return NOSV_ERR_INVALID_OPERATION;

	if (is_blocking || is_inline) {
		// These submit modes cannot be used from outside a task context
		if (!worker_is_in_task())
			return NOSV_ERR_OUTSIDE_TASK;
	}

	// This combination would make no sense
	if (is_inline && task_is_parallel(task))
		return NOSV_ERR_INVALID_OPERATION;

	// Parallel tasks cannot block
	if (is_dl_wake && task_is_parallel(task))
		return NOSV_ERR_INVALID_OPERATION;
	if (is_blocking && task_is_parallel(worker_current_task()))
		return NOSV_ERR_INVALID_OPERATION;

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
		task->wakeup = worker->handle.task;

	int32_t count;

	// Entry point - The task became ready
	monitoring_task_changed_status(task, ready_status);

	// If we have an immediate successor we don't place the task into the scheduler
	// However, if we're not in a worker context, or we are currently executing a task,
	// we will ignore the request, as it would hang the program.
	// Additionally, we will reject to place as IS parallel tasks
	if (is_immediate && worker && !worker->in_task_body && !task_is_parallel(task)) {
		if (worker_get_immediate()) {
			// Setting a new immediate successor, but there was already one.
			// Place the new one and send the old one to the scheduler
			scheduler_batch_submit(worker_get_immediate());
		}

		count = atomic_fetch_sub_explicit(&task->blocking_count, 1, memory_order_relaxed) - 1;
		assert(count == 0);

		worker_set_immediate(task);
	} else if (is_inline) {
		nosv_flush_submit_window();

		nosv_worker_t *worker = worker_current();
		assert(worker);

		count = atomic_fetch_sub_explicit(&task->blocking_count, 1, memory_order_relaxed) - 1;
		assert(count == 0);

		task_execution_handle_t old_handle = worker->handle;
		assert(old_handle.task);

		uint32_t old_bodyid = instr_get_bodyid(old_handle);
		instr_task_pause((uint32_t)old_handle.task->taskid, old_bodyid);

		task_execution_handle_t handle = {
			.task = task,
			.execution_id = 1
		};
		task_execute(handle);

		instr_task_resume((uint32_t)old_handle.task->taskid, old_bodyid);

		// Restore old task
		worker->handle = old_handle;
	} else if (is_dl_wake) {
		deadline_state_t state = atomic_exchange_explicit(&task->deadline_state, NOSV_DEADLINE_READY, memory_order_relaxed);
		if (state == NOSV_DEADLINE_WAITING)
			scheduler_request_deadline_purge();
	} else {
		count = atomic_fetch_sub_explicit(&task->blocking_count, 1, memory_order_relaxed) - 1;

		if (count == 0)
			scheduler_batch_submit(task);
	}

	if (is_blocking)
		task_pause(current_task, /* use_blocking_count */ 1);

	if (current_task) {
		hwcounters_update_runtime_counters();
		monitoring_task_changed_status(current_task, executing_status);
	}

	instr_submit_exit();

	return NOSV_SUCCESS;
}

void task_pause(
	nosv_task_t task,
	int use_blocking_count)
{
	nosv_worker_t *worker = worker_current();

	assert(task);
	assert(task == worker_current_task());
	assert(!task_is_parallel(task));

	nosv_flush_submit_window();

	// Thread might yield, read and accumulate hardware counters for the task that blocks
	hwcounters_update_task_counters(task);
	monitoring_task_changed_status(task, paused_status);

	uint32_t bodyid = instr_get_bodyid(worker->handle);
	instr_task_pause((uint32_t)task->taskid, bodyid);

	int32_t count = 1;
	if (use_blocking_count)
		count = atomic_fetch_add_explicit(&task->blocking_count, 1, memory_order_relaxed) + 1;

	// If r < 1, we have already been unblocked
	if (count > 0)
		worker_yield();

	assert(atomic_load_explicit(&task->blocking_count, memory_order_relaxed) <= 0);

	// Thread might have been resumed here, read and accumulate hardware counters for the CPU
	hwcounters_update_runtime_counters();
	monitoring_task_changed_status(task, executing_status);

	instr_task_resume((uint32_t)task->taskid, bodyid);
}

/* Blocking, yield operation */
/* Callable from a task context ONLY */
int nosv_pause(
	nosv_flags_t flags)
{
	// We have to be inside a worker
	nosv_worker_t *worker = worker_current();
	if (!worker || !worker->handle.task)
		return NOSV_ERR_OUTSIDE_TASK;

	if (kinstr)
		instr_kernel_flush(kinstr);

	nosv_flush_submit_window();

	nosv_task_t task = worker_current_task();
	assert(task);

	// Parallel tasks cannot be blocked
	if (task_is_parallel(task))
		return NOSV_ERR_INVALID_OPERATION;

	instr_pause_enter();

	task_pause(task, /* use_blocking_count */1);

	instr_pause_exit();

	return NOSV_SUCCESS;
}

int nosv_cancel(
	nosv_flags_t flags)
{
	if (!worker_is_in_task())
		return NOSV_ERR_OUTSIDE_TASK;

	nosv_task_t task = worker_current_task();
	assert(task);

	int32_t degree = task_get_degree(task);
	assert(degree != 0);

	do {
		if (degree < 0)
			return NOSV_ERR_INVALID_OPERATION;
	} while (!atomic_compare_exchange_weak_explicit(&(task->degree), &degree, -degree, memory_order_relaxed, memory_order_relaxed));

	return NOSV_SUCCESS;
}

/* Deadline tasks */
int nosv_waitfor(
	uint64_t target_ns,
	uint64_t *actual_ns /* out */)
{
	// We have to be inside a worker
	nosv_worker_t *worker = worker_current();
	if (!worker || !worker->handle.task)
		return NOSV_ERR_OUTSIDE_TASK;

	if (kinstr)
		instr_kernel_flush(kinstr);

	nosv_task_t task = worker->handle.task;

	// Parallel tasks cannot be blocked
	if (task_is_parallel(task))
		return NOSV_ERR_INVALID_OPERATION;

	nosv_flush_submit_window();

	// Thread is gonna yield, read and accumulate hardware counters for the task
	hwcounters_update_task_counters(task);
	monitoring_task_changed_status(task, ready_status);

	instr_waitfor_enter();
	uint32_t bodyid = instr_get_bodyid(worker->handle);
	instr_task_pause((uint32_t)task->taskid, bodyid);

	const uint64_t start_ns = clock_ns();
	task->deadline = start_ns + target_ns;

	// Only sleep if the task has not been attempted to be woken up yet
	deadline_state_t expected = NOSV_DEADLINE_NONE;
	deadline_state_t desired = NOSV_DEADLINE_PENDING;
	if (atomic_compare_exchange_strong_explicit(&task->deadline_state, &expected, desired, memory_order_relaxed, memory_order_relaxed)) {
		// Submit the task to re-schedule when the deadline is done
		// Forego the current
		scheduler_submit_single(task);
		// Block until the deadline expires
		worker_yield();
	} else {
		assert(expected == NOSV_DEADLINE_READY);
	}
	atomic_store_explicit(&task->deadline_state, NOSV_DEADLINE_NONE, memory_order_relaxed);

	// Unblocked
	task->deadline = 0;

	if (actual_ns)
		*actual_ns = clock_ns() - start_ns;

	// Thread has been resumed, read and accumulate hardware counters for the CPU
	hwcounters_update_runtime_counters();
	monitoring_task_changed_status(task, executing_status);

	instr_task_resume((uint32_t)task->taskid, bodyid);
	instr_waitfor_exit();

	return NOSV_SUCCESS;
}

/* Yield operation */
/* Restriction: Can only be called from a task context */
int nosv_yield(
	nosv_flags_t flags)
{
	if (kinstr)
		instr_kernel_flush(kinstr);

	if (!worker_is_in_task())
		return NOSV_ERR_OUTSIDE_TASK;

	if (!(flags & NOSV_YIELD_NOFLUSH))
		nosv_flush_submit_window();

	nosv_task_t task = worker_current_task();
	assert(task);

	// Parallel tasks cannot be blocked
	if (task_is_parallel(task))
		return NOSV_ERR_INVALID_OPERATION;

	// Thread is gonna yield, read and accumulate hardware counters for the task
	hwcounters_update_task_counters(task);
	monitoring_task_changed_status(task, ready_status);

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

	return NOSV_SUCCESS;
}

int nosv_schedpoint(
	nosv_flags_t flags)
{
	int pid, yield, cpuid;
	uint64_t ts;

	if (kinstr)
		instr_kernel_flush(kinstr);

	if (!worker_is_in_task())
		return NOSV_ERR_OUTSIDE_TASK;

	nosv_flush_submit_window();

	nosv_task_t task = worker_current_task();
	assert(task);

	// Parallel tasks cannot be blocked
	if (task_is_parallel(task))
		return NOSV_ERR_INVALID_OPERATION;

	// Thread is gonna yield, read and accumulate hardware counters for the task
	hwcounters_update_task_counters(task);
	monitoring_task_changed_status(task, ready_status);

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

	return NOSV_SUCCESS;
}

/* Callable from everywhere */
int nosv_destroy(
	nosv_task_t task,
	nosv_flags_t flags)
{
	if (unlikely(!task))
		return NOSV_ERR_INVALID_PARAMETER;

	instr_destroy_enter();

	sfree(task, sizeof(struct nosv_task) +
		task->metadata +
		hwcounters_get_task_size() +
		monitoring_get_task_size(),
		cpu_get_current()
	);

	instr_destroy_exit();

	return NOSV_SUCCESS;
}

static inline void task_complete(nosv_task_t task)
{
	// Entry point - A task has just completed its execution
	monitoring_task_completed(task);

	nosv_task_t wakeup = task->wakeup;
	task->wakeup = NULL;

	atomic_store_explicit(&task->event_count, 1, memory_order_relaxed);
	task->scheduled_count = 0;

	if (task->type->completed_callback)
		task->type->completed_callback(task);
	// From here, task may be freed!

	if (wakeup)
		nosv_submit(wakeup, NOSV_SUBMIT_UNLOCKED);
}

void task_execute(task_execution_handle_t handle)
{
	nosv_task_t task = handle.task;
	assert(task);
	nosv_worker_t *worker = worker_current();
	assert(worker);

	if (!task_is_parallel(task))
		task->worker = worker;
	worker->handle = handle;

	// Task is about to execute, update runtime counters
	hwcounters_update_runtime_counters();
	monitoring_task_changed_status(task, executing_status);

	const uint32_t taskid = (uint32_t) task->taskid;
	const uint32_t bodyid = instr_get_bodyid(handle);
	instr_task_execute(taskid, bodyid);

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

	nosv_flush_submit_window();

	// Task just completed, read and accumulate hardware counters for the task
	hwcounters_update_task_counters(task);
	monitoring_task_changed_status(task, paused_status);

	// After the run and end callbacks, we can safely reset the task in case it has to be re-entrant
	// Reset the blocking count
	atomic_store_explicit(&task->blocking_count, 1, memory_order_relaxed);
	// Remove stack frame
	// Remove the worker assigned to this task
	task->worker = NULL;
	// Remove the task assigned to the worker as well
	worker->handle = EMPTY_TASK_EXECUTION_HANDLE;

	uint64_t res = atomic_fetch_sub_explicit(&task->event_count, 1, memory_order_relaxed) - 1;
	if (!res) {
		task_complete(task);
	}
	// Warning: from this point forward, "task" may have been freed, and thus it is not safe to access

	instr_task_end(taskid, bodyid);
}

/* Events API */
/* Restriction: Can only be called from a task context */
int nosv_increase_event_counter(
	uint64_t increment)
{
	if (!increment)
		return NOSV_ERR_INVALID_PARAMETER;

	nosv_task_t current = worker_current_task();
	if (!current)
		return NOSV_ERR_OUTSIDE_TASK;

	atomic_fetch_add_explicit(&current->event_count, increment, memory_order_relaxed);

	return NOSV_SUCCESS;
}

/* Restriction: Can only be called from a nOS-V Worker */
int nosv_decrease_event_counter(
	nosv_task_t task,
	uint64_t decrement)
{
	if (!task)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!decrement)
		return NOSV_ERR_INVALID_PARAMETER;

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

	return NOSV_SUCCESS;
}

/*
	Attach "adopts" an external thread. We have to create a nosv_worker to represent this thread,
	and create an implicit task. Note that the task's callbacks will not be called, and we will
	consider it an error.
	The task will be placed with an attached worker into the scheduler, and the worker will be blocked.
*/
int nosv_attach(
	nosv_task_t *task /* out */,
	nosv_affinity_t *affinity,
	const char * label,
	nosv_flags_t flags)
{
	instr_attach_enter();

	if (unlikely(!task)) {
		instr_attach_exit();
		return NOSV_ERR_INVALID_PARAMETER;
	}

	// Mind nested nosv_attach and nosv_detach
	if (rt_attach_refcount++) {
		// Check that between nosv_attaches turbo has not changed
		worker_check_turbo();
		instr_attach_exit();
		return NOSV_SUCCESS;
	}

	assert(!worker_current());

	nosv_task_type_t type;
	int ret = nosv_type_init(&type, NULL, NULL, NULL, label, NULL, NULL, NOSV_TYPE_INIT_EXTERNAL);
	if (ret != NOSV_SUCCESS) {
		instr_attach_exit();
		return ret;
	}

	nosv_worker_t *worker = worker_create_external();
	assert(worker);

	ret = nosv_create_internal(task, type, 0, NOSV_CREATE_NONE);
	if (ret) {
		worker_free_external(worker);
		instr_attach_exit();
		return ret;
	}

	// We created the task fine. Now map the task to the worker
	nosv_task_t t = *task;
	task_execution_handle_t handle = {
		.task = t,
		.execution_id = 1
	};

	t->worker = worker;
	worker->handle = handle;

	// Set the affinity if required
	if (affinity != NULL) {
		t->affinity = *affinity;
	}

	atomic_fetch_sub_explicit(&t->blocking_count, 1, memory_order_relaxed);

	// Register worker in the affinity support facility
	affinity_support_register_worker(worker, 0);

	// Entry point - A task becomes ready
	monitoring_task_changed_status(t, ready_status);

	// Submit task for scheduling at an actual CPU
	scheduler_submit_single(t);

	// Block the worker
	worker_block();
	// Now we have been scheduled, return

	// Task is about to execute, update runtime counters
	hwcounters_update_runtime_counters();
	monitoring_task_changed_status(t, executing_status);

	instr_attach_exit();

	// Inform the instrumentation about the new task being in
	// execution, as it won't pass via task_execute()
	instr_task_execute((uint32_t)t->taskid, instr_get_bodyid(handle));

	return NOSV_SUCCESS;
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
	if (!worker || !worker->handle.task)
		return NOSV_ERR_OUTSIDE_TASK;

	// Check that between nosv_detaches or between
	// attach/detach turbo has not changed
	worker_check_turbo();

	// Mind nested nosv_attach and nosv_detach
	if (--rt_attach_refcount)
		return NOSV_SUCCESS;

	nosv_task_t task = worker->handle.task;

	// Task just completed, read and accumulate hardware counters for the task
	hwcounters_update_task_counters(task);
	monitoring_task_completed(task);

	instr_task_end((uint32_t)task->taskid, instr_get_bodyid(worker->handle));

	// Delay detach enter event, so we finish the "in task body" state first
	instr_detach_enter();

	// First, free both the task and the type
	nosv_task_type_t type = task->type;
	assert(type);
	nosv_destroy(task, NOSV_DESTROY_NONE);
	nosv_type_destroy(type, NOSV_DESTROY_NONE);

	cpu_t *cpu = worker->cpu;
	assert(cpu);

	// This thread is no longer controled by nosv, and from now on it might
	// overlap with other nosv threads. Therefore, we mark is as a "cooling"
	// thread.
	instr_thread_cool();

	affinity_support_unregister_worker(worker, !(flags & NOSV_DETACH_NO_RESTORE_AFFINITY));

	// Now free the worker
	// We have to free before waking up another worker on the current CPU
	// Otherwise the sfree inside worker_free_external does not have exclusive access to the cpu buckets
	worker_free_external(worker);

	// Now before waking another thread, let's unset our cpu
	// Otherwise following operations after this thread has detached may use the wrong CPU when accessing
	// the SLAB allocator
	cpu_set_current(-1);

	// Then resume a thread on the current cpu
	task_execution_handle_t handle = EMPTY_TASK_EXECUTION_HANDLE;
	worker_wake_idle(logic_pid, cpu, handle);

	instr_detach_exit();
	return NOSV_SUCCESS;
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

void nosv_set_task_degree(nosv_task_t task, int32_t degree)
{
	assert(degree > 0);
	assert(task);
	assert(task->flags & NOSV_CREATE_PARALLEL);
	atomic_store_explicit(&(task->degree), degree, memory_order_relaxed);
}

int32_t nosv_get_task_degree(nosv_task_t task)
{
	return task_get_degree(task);
}

uint32_t nosv_get_execution_id(void)
{
	if (!worker_is_in_task())
		return NOSV_ERR_OUTSIDE_TASK;

	nosv_worker_t *worker = worker_current();
	assert(worker);

	return worker->handle.execution_id - 1;
}

nosv_affinity_t nosv_get_default_affinity(void)
{
	return default_affinity;
}

nosv_task_t nosv_self(void)
{
	return worker_current_task();
}

int nosv_flush_submit_window(void)
{
	nosv_task_t current_task = worker_current_task();
	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	if (!task_group_empty(&current_task->submit_window)) {
		scheduler_submit_group(&current_task->submit_window);
		task_group_clear(&current_task->submit_window);
	}

	return NOSV_SUCCESS;
}

int nosv_set_submit_window_size(size_t submit_window_size)
{
	if (unlikely(submit_window_size == 0))
		return NOSV_ERR_INVALID_PARAMETER;

	nosv_task_t current_task = worker_current_task();
	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	current_task->submit_window_maxsize = submit_window_size;

	if (submit_window_size == 1)
		nosv_flush_submit_window();

	return NOSV_SUCCESS;
}
