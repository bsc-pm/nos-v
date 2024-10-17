/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_H
#define NOSV_H

#define __NOSV__

#include <stdint.h>
#include <sys/types.h>

#include "nosv/error.h"

#pragma GCC visibility push(default)

#ifdef __cplusplus
extern "C" {
#endif

/* 	The maximum size for metadata embedded in tasks is 4Kbytes
	For more, embed a pointer into the structure and allocate metadata separately
*/
#define NOSV_MAX_METADATA_SIZE 4096

/* Support Macros */
#define __ZEROBITS ((uint64_t)0)
#define __BIT(n) (((uint64_t)1) << (n))

typedef uint64_t nosv_flags_t;

// Opaque types for task types and tasks
struct nosv_task_type;
typedef struct nosv_task_type *nosv_task_type_t;
struct nosv_task;
typedef struct nosv_task *nosv_task_t;
typedef struct nosv_affinity nosv_affinity_t;
struct nosv_mutex;
typedef struct nosv_mutex *nosv_mutex_t;
struct nosv_barrier;
typedef struct nosv_barrier *nosv_barrier_t;

typedef void (*nosv_task_run_callback_t)(nosv_task_t);
typedef void (*nosv_task_end_callback_t)(nosv_task_t);
typedef void (*nosv_task_completed_callback_t)(nosv_task_t);
typedef uint64_t (*nosv_cost_function_t)(nosv_task_t);

/* Read-only task attributes */
/* Returns a pointer to the metadata or NULL if nothing was allocated */
void *nosv_get_task_metadata(nosv_task_t task);
nosv_task_type_t nosv_get_task_type(nosv_task_t task);

/* Read-write task attributes */
/* Note that getting priority while another thread is setting it, or
   setting the priority when the task has been submitted has undefined
   behaviour */
int nosv_get_task_priority(nosv_task_t task);
void nosv_set_task_priority(nosv_task_t task, int priority);
/* Set how many -potentially concurrent- times should a task be run before ending */
/* Degree should be > 0 and only set in parallel tasks */
int32_t nosv_get_task_degree(nosv_task_t task);
void nosv_set_task_degree(nosv_task_t task, int32_t degree);
/* Callable from task context */
uint32_t nosv_get_execution_id(void);

/* Read-only task type attributes */
nosv_task_run_callback_t nosv_get_task_type_run_callback(nosv_task_type_t type);
nosv_task_end_callback_t nosv_get_task_type_end_callback(nosv_task_type_t type);
nosv_task_completed_callback_t nosv_get_task_type_completed_callback(nosv_task_type_t type);
const char *nosv_get_task_type_label(nosv_task_type_t type);
void *nosv_get_task_type_metadata(nosv_task_type_t type);

/* Initialize nOS-V */
int nosv_init(void);

/* Shutdown nOS-V */
int nosv_shutdown(void);

/* Get own task descriptor, NULL if we are not a task */
nosv_task_t nosv_self(void);

/* Flags */
#define NOSV_TYPE_INIT_NONE 		__ZEROBITS
/* This type represents an external thread, and has no callbacks */
#define NOSV_TYPE_INIT_EXTERNAL		__BIT(0)

/* Create a task type with certain run/end callbacks and a label */
int nosv_type_init(
	nosv_task_type_t *type /* out */,
	nosv_task_run_callback_t run_callback,
	nosv_task_end_callback_t end_callback,
	nosv_task_completed_callback_t completed_callback,
	const char *label,
	void *metadata,
	nosv_cost_function_t cost_function,
	nosv_flags_t flags);

/* Flags */
#define NOSV_TYPE_DESTROY_NONE __ZEROBITS

/* Destroy a task type */
int nosv_type_destroy(
	nosv_task_type_t type,
	nosv_flags_t flags);

/* Flags */
#define NOSV_CREATE_NONE        __ZEROBITS
/* Create a parallel task that can run multiple times */
#define NOSV_CREATE_PARALLEL    __BIT(0)

/* May return -ENOMEM. 0 on success */
/* Callable from everywhere */
int nosv_create(
	nosv_task_t *task /* out */,
	nosv_task_type_t type,
	size_t metadata_size,
	nosv_flags_t flags);

/* Flags */
#define NOSV_SUBMIT_NONE          __ZEROBITS
/* Hint nOS-V that this task has just been unlocked, and give it scheduling priority */
#define NOSV_SUBMIT_UNLOCKED      __BIT(0)
/* Block the current task until the submitted task has completed */
#define NOSV_SUBMIT_BLOCKING      __BIT(1)
/* Hint nOS-V to execute this task in the same CPU where the current task is running, immediately after finishing or pausing it */
/* This flag can improve the cache locality exploitation if the successor accesses common data subsets. The flag also reduces the traffic in the scheduler */
#define NOSV_SUBMIT_IMMEDIATE     __BIT(2)
/* Execute this task inline, substituting the currently running task for this worker */
#define NOSV_SUBMIT_INLINE        __BIT(3)
/* Wake up a waitfor task even if its timeout has not expired yet */
#define NOSV_SUBMIT_DEADLINE_WAKE __BIT(4)

/* Callable from everywhere */
int nosv_submit(
	nosv_task_t task,
	nosv_flags_t flags);

/* Flags */
#define NOSV_PAUSE_NONE __ZEROBITS

/* Blocking, yield operation */
/* Restriction: Can only be called from a task context */
int nosv_pause(
	nosv_flags_t flags);

/* Flags */
#define NOSV_CANCEL_NONE __ZEROBITS
/* Only callable from task context */
int nosv_cancel(
	nosv_flags_t flags);

/* Deadline tasks */
int nosv_waitfor(
	uint64_t target_ns,
	uint64_t *actual_ns /* out */);

/* Flags */
#define NOSV_YIELD_NONE 	__ZEROBITS
/* Do not flush the submit window when yielding */
#define NOSV_YIELD_NOFLUSH 	__BIT(0)

/* Yield operation */
/* Restriction: Can only be called from a task context */
int nosv_yield(
	nosv_flags_t flags);

#define NOSV_SCHEDPOINT_NONE __ZEROBITS

/* Scheduling point operation */
/* Restriction: Can only be called from a task context */
int nosv_schedpoint(
	nosv_flags_t flags);

/* Flags */
#define NOSV_DESTROY_NONE __ZEROBITS

/* Callable from everywhere */
int nosv_destroy(
	nosv_task_t task,
	nosv_flags_t flags);

/* Events API */
/* Restriction: Can only be called from a task context */
int nosv_increase_event_counter(
	uint64_t increment);

/* Return if the current task has events */
/* This call is intended as a hint */
/* Another thread can decrease the events at any time */
/* The only case where the result is guaranteed to be correct is when there are no events */
/* Restriction: Can only be called from a task context */
int nosv_has_events(void);

/* Restriction: Can only be called from a nOS-V Worker */
int nosv_decrease_event_counter(
	nosv_task_t task,
	uint64_t decrement);

/* Flags */
#define NOSV_ATTACH_NONE			__ZEROBITS

/* Thread Attaching API */
int nosv_attach(
	nosv_task_t *task /* out */,
	nosv_affinity_t *affinity,
	const char *label,
	nosv_flags_t flags);

/* Flags */
#define NOSV_DETACH_NONE				__ZEROBITS
/* Do not restore the original affinity of the attached thread */
#define NOSV_DETACH_NO_RESTORE_AFFINITY	__BIT(0)

/* Called from attached thread */
int nosv_detach(
	nosv_flags_t flags);

/* Flags */
#define NOSV_MUTEX_NONE __ZEROBITS

/* Initialize a nosv_mutex_t object. The attr object is currently not
 * implemented, use NULL */
int nosv_mutex_init(
	nosv_mutex_t *mutex,
	nosv_flags_t flags);

/* Destroys a nosv_mutex_t object */
int nosv_mutex_destroy(
	nosv_mutex_t mutex);

/* Similar to pthread_mutex_lock, locks a mutex but calls nosv_pause if the lock
 * is contended */
/* Restriction: Can only be called from a task context */
int nosv_mutex_lock(
	nosv_mutex_t mutex);

/* Lock the mutex or return immediately if contended */
/* Restriction: Can only be called from a task context */
int nosv_mutex_trylock(
	nosv_mutex_t mutex);

/* Unlock a mutex object */
/* Restriction: Can only be called from a task context */
int nosv_mutex_unlock(
	nosv_mutex_t mutex);

/* Flags */
#define NOSV_BARRIER_NONE __ZEROBITS

/* Initialize the "barrier" object to wait for "count" threads */
int nosv_barrier_init(
	nosv_barrier_t *barrier,
	nosv_flags_t flags,
	unsigned count);

/* Destroy the "barrier" object. It can be re-initialized afterwards */
int nosv_barrier_destroy(
	nosv_barrier_t barrier);

/* Similar to pthread_barrier_wait, block until "count" threads have reached the
 * barrier */
/* Restriction: Can only be called from a task context */
int nosv_barrier_wait(
	nosv_barrier_t barrier);

/* Batch Submit API */
/* Set the maximum size of the submit window */
/* submit_batch_size > 0 */
/* Restriction: Can only be called from a task context */
int nosv_set_submit_window_size(size_t submit_batch_size);

/* Flush the current submit window */
/* Restriction: Can only be called from a task context */
int nosv_flush_submit_window(void);

/* Suspend API */
/* Suspend Modes */
typedef enum nosv_suspend_mode {
	NOSV_SUSPEND_MODE_NONE = 0,
	NOSV_SUSPEND_MODE_SUBMIT,
	NOSV_SUSPEND_MODE_TIMEOUT_SUBMIT,
	NOSV_SUSPEND_MODE_EVENT_SUBMIT,
} nosv_suspend_mode_t;

/* Set a suspend mode and its arguments if needed */
/* Restriction: Can only be called from a task context */
int nosv_set_suspend_mode(nosv_suspend_mode_t suspend_mode, uint64_t args);

/* Marks the task to suspend, the task will suspend, instead of complete, when returns from the body */
/* Restriction: Can only be called from a task context */
int nosv_suspend(void);

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop

#endif // NOSV_H
