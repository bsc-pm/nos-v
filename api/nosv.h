/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_H
#define NOSV_H

#define __NOSV__

#include <stdint.h>
#include <sys/types.h>

/* 	The maximum size for metadata embedded in tasks is 4Kbytes
	For more, embed a pointer into the structure and allocate metadata separately
*/
#define NOSV_MAX_METADATA_SIZE 4096

/* Support Macros */
#define __ZEROBITS ((uint64_t) 0)
#define __BIT(n) ((uint64_t) (1 << n))

typedef uint64_t nosv_flags_t;

// Opaque types for task types and tasks
struct nosv_task_type;
typedef struct nosv_task_type *nosv_task_type_t;
struct nosv_task;
typedef struct nosv_task *nosv_task_t;

typedef void (*nosv_task_run_callback_t)(nosv_task_t);
typedef void (*nosv_task_end_callback_t)(nosv_task_t);

/* Read-only task attributes */
void *nosv_get_task_metadata(nosv_task_t task);
nosv_task_type_t nosv_get_task_type(nosv_task_t task);

/* Read-write task attributes */
int nosv_get_task_priority(nosv_task_t task);
void nosv_set_task_priority(nosv_task_t task, int priority);

/* Read-only task type attributes */
nosv_task_run_callback_t nosv_get_task_type_run_callback(nosv_task_type_t type);
nosv_task_end_callback_t nosv_get_task_type_end_callback(nosv_task_type_t type);
const char *nosv_get_task_type_label(nosv_task_type_t type);
void *nosv_get_task_type_metadata(nosv_task_type_t type);

/* Initialize nOS-V */
int nosv_init();

/* Shutdown nOS-V */
int nosv_shutdown();

/* Flags */
#define NOSV_TYPE_INIT_NONE 	__ZEROBITS

/* Create a task type with certain run/end callbacks and a label */
int nosv_type_init(
	nosv_task_type_t *type /* out */,
	nosv_task_run_callback_t run_callback,
	nosv_task_end_callback_t end_callback,
	const char *label,
	void *metadata,
	nosv_flags_t flags);

/* Flags */
#define NOSV_CREATE_NONE 		__ZEROBITS
#define NOSV_CREATE_TASKFOR 	__BIT( 0)

/* May return -ENOMEM. 0 on success */
/* Callable from everywhere */
int nosv_create(
	nosv_task_t *task /* out */,
	nosv_task_type_t type,
	size_t metadata_size,
	nosv_flags_t flags);

/* Flags */
#define NOSV_SUBMIT_NONE 		__ZEROBITS
#define NOSV_SUBMIT_UNLOCKED 	__BIT( 0)

/* Callable from everywhere */
int nosv_submit(
	nosv_task_t task,
	nosv_flags_t flags);

/* Flags */
#define NOSV_PAUSE_NONE 		__ZEROBITS

/* Blocking, yield operation */
/* Callable from a task context ONLY */
int nosv_pause(
	nosv_flags_t flags);

/* Deadline tasks */
int nosv_waitfor(
	uint64_t ns);

/* Flags */
#define NOSV_DESTROY_NONE 		__ZEROBITS

/* Callable from everywhere */
int nosv_destroy(
	nosv_task_t task,
	nosv_flags_t flags);

#endif // NOSV_H