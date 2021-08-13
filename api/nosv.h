/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_H
#define NOSV_H

#define __NOSV__

#include <stdint.h>
#include <sys/types.h>

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
#define __BIT(n) ((uint64_t)(1 << n))

typedef uint64_t nosv_flags_t;

// Opaque types for task types and tasks
struct nosv_task_type;
typedef struct nosv_task_type *nosv_task_type_t;
struct nosv_task;
typedef struct nosv_task *nosv_task_t;
typedef struct nosv_affinity nosv_affinity_t;

typedef void (*nosv_task_run_callback_t)(nosv_task_t);
typedef void (*nosv_task_end_callback_t)(nosv_task_t);
typedef void (*nosv_task_completed_callback_t)(nosv_task_t);

/* Read-only task attributes */
void *nosv_get_task_metadata(nosv_task_t task);
nosv_task_type_t nosv_get_task_type(nosv_task_t task);

/* Read-write task attributes */
int nosv_get_task_priority(nosv_task_t task);
void nosv_set_task_priority(nosv_task_t task, int priority);

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
	nosv_flags_t flags);

/* Flags */
#define NOSV_TYPE_DESTROY_NONE __ZEROBITS

/* Destroy a task type */
int nosv_type_destroy(
	nosv_task_type_t type,
	nosv_flags_t flags);

/* Flags */
#define NOSV_CREATE_NONE 		__ZEROBITS
#define NOSV_CREATE_TASKFOR 	__BIT(0)

/* May return -ENOMEM. 0 on success */
/* Callable from everywhere */
int nosv_create(
	nosv_task_t *task /* out */,
	nosv_task_type_t type,
	size_t metadata_size,
	nosv_flags_t flags);

/* Flags */
#define NOSV_SUBMIT_NONE 		__ZEROBITS
/* Hint nOS-V that this task has just been unlocked, and give it scheduling priority */
#define NOSV_SUBMIT_UNLOCKED 	__BIT(0)
/* Block the current task until the submitted task has completed */
#define NOSV_SUBMIT_BLOCKING	__BIT(1)
/* Hint nOS-V to execute this task in the same thread as the one currently executing, immediately after */
#define NOSV_SUBMIT_IMMEDIATE	__BIT(2)
/* Execute this task inline, substituting the currently running task for this worker */
#define NOSV_SUBMIT_INLINE		__BIT(3)

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

/* Deadline tasks */
int nosv_waitfor(
	uint64_t ns);

/* Flags */
#define NOSV_YIELD_NONE __ZEROBITS

/* Yield operation */
/* Restriction: Can only be called from a task context */
int nosv_yield(
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

/* Restriction: Can only be called from a nOS-V Worker */
int nosv_decrease_event_counter(
	nosv_task_t task,
	uint64_t decrement);

/* Flags */
#define NOSV_ATTACH_NONE __ZEROBITS

/* Thread Attaching API */
int nosv_attach(
	nosv_task_t *task /* out */,
	nosv_task_type_t type /* must have null callbacks */,
	size_t metadata_size,
	nosv_affinity_t *affinity,
	nosv_flags_t flags);

/* Flags */
#define NOSV_DETACH_NONE __ZEROBITS

/* Called from attached thread */
int nosv_detach(
	nosv_flags_t flags);

/* CPU Information API */
/* Get number of CPUs leveraged by the nOS-V runtime */
int nosv_get_num_cpus(void);

/* Get the logical identifier of the CPU where the current task is running */
/* The range of logical identifiers is [0, number of cpus) */
/* Restriction: Can only be called from a task context */
int nosv_get_current_logical_cpu(void);

/* Get the system identifier of the CPU where the current task is running */
/* Restriction: Can only be called from a task context */
int nosv_get_current_system_cpu(void);

#ifdef __cplusplus
}
#endif

#pragma GCC visibility pop

#endif // NOSV_H
