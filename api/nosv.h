/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_H
#define NOSV_H

#define __NOSV__

#include <stdint.h>
#include <sys/types.h>

typedef uint64_t nosv_flags_t;

struct nosv_task;

typedef void (*nosv_task_run_callback_t)(struct nosv_task *);
typedef void (*nosv_task_end_callback_t)(struct nosv_task *);

struct nosv_task_type {
	// Public Fields
	nosv_task_run_callback_t run_callback;
	nosv_task_end_callback_t end_callback;
	const char *label;
};

typedef struct nosv_task_type * nosv_task_type_t;

struct nosv_task {
	// Public Fields
	nosv_task_type_t type;
	void *custom_metadata; /* Usable by nOS-V users */
};

typedef struct nosv_task nosv_task_t;

/* Wrap everything in macros for sanity? */
#define nosv_task_metadata(t) (t->custom_metadata)

/* Create a task type with certain run/end callbacks and a label */
int nosv_init_type(nosv_task_type_t *type /* out */, nosv_task_run_callback_t run_callback, nosv_task_end_callback_t end_callback, const char *label);

/* May return -ENOMEM. 0 on success */
/* Callable from everywhere */
int nosv_create(nosv_task_t **task /* out */, nosv_task_type_t *type, size_t metadata_bytes, nosv_flags_t flags);

/* Callable from everywhere */
int nosv_submit(nosv_task_t *task, nosv_flags_t flags);

/* Blocking, yield operation */
/* Callable from a task context ONLY */
int nosv_pause(nosv_flags_t flags);

/* Deadline tasks */
int nosv_waitfor(uint64_t ns);

/* Callable from everywhere */
int nosv_destroy(nosv_task_t *task, nosv_flags_t flags);

#endif // NOSV_H