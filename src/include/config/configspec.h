/*
	This file is part of Nanos6 and nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2020-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef CONFIG_PARSE_H
#define CONFIG_PARSE_H

#include <limits.h>
#include <stddef.h>

#include "compiler.h"
#include "config.h"

#define MAX_CONFIG_PATH PATH_MAX

enum config_spec_type {
	TYPE_INT64    = 0, /* Signed 64-bit integer */
	TYPE_PTR      = 1, /* Pointer */
	TYPE_UINT64   = 2, /* Unsigned 64-bit Integer */
	TYPE_SIZE     = 3, /* Size in string mode */
	TYPE_STR      = 4, /* String */
	TYPE_BOOL     = 5, /* Boolean */
	TYPE_LIST_STR = 6, /* List of strings */
};

typedef struct config_spec {
	int type;
	const char *name;
	unsigned member_offset;
	unsigned member_size;
} config_spec_t;

// This looks silly, but we get in return an iterable array of configuration
// options complete with its default values...

#define member_size(type, member) sizeof(((type *)0)->member)

#define DECLARE_CONFIG(_type_, _fullname_, _member_) \
	{ \
		.type = (_type_), \
		.name = (_fullname_), \
		.member_offset = offsetof(rt_config_t, _member_), \
		.member_size = member_size(rt_config_t, _member_) \
	}

static config_spec_t config_spec_list[] = {
	DECLARE_CONFIG(TYPE_UINT64, "scheduler.quantum_ns", sched_quantum_ns),
	DECLARE_CONFIG(TYPE_UINT64, "scheduler.queue_batch", sched_batch_size),
	DECLARE_CONFIG(TYPE_UINT64, "scheduler.cpus_per_queue", sched_cpus_per_queue),
	DECLARE_CONFIG(TYPE_UINT64, "scheduler.in_queue_size", sched_in_queue_size),
	DECLARE_CONFIG(TYPE_STR, "shared_memory.name", shm_name),
	DECLARE_CONFIG(TYPE_PTR, "shared_memory.start", shm_start),
	DECLARE_CONFIG(TYPE_SIZE, "shared_memory.size", shm_size),
	DECLARE_CONFIG(TYPE_BOOL, "debug.dump_config", debug_dump_config),
	DECLARE_CONFIG(TYPE_BOOL, "hwcounters.verbose", hwcounters_verbose),
	DECLARE_CONFIG(TYPE_STR, "hwcounters.backend", hwcounters_backend),
	DECLARE_CONFIG(TYPE_LIST_STR, "hwcounters.papi_events", hwcounters_papi_events),
	DECLARE_CONFIG(TYPE_BOOL, "turbo.enabled", turbo_enabled),
};

#endif // CONFIG_PARSE_H
