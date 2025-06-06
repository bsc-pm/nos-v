/*
	This file is part of Nanos6 and nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2020-2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef CONFIG_PARSE_H
#define CONFIG_PARSE_H

#include <limits.h>
#include <stddef.h>

#include "compiler.h"
#include "config.h"

#define MAX_CONFIG_PATH PATH_MAX

enum config_spec_type {
	TYPE_INT64			= 0, /* Signed 64-bit integer */
	TYPE_PTR			= 1, /* Pointer */
	TYPE_UINT64			= 2, /* Unsigned 64-bit Integer */
	TYPE_SIZE			= 3, /* Size in string mode */
	TYPE_STR			= 4, /* String */
	TYPE_BOOL			= 5, /* Boolean */
	TYPE_LIST_STR		= 6, /* List of strings */
};

static const size_t config_spec_type_size[] = {
	sizeof(int64_t),
	sizeof(void *),
	sizeof(uint64_t),
	sizeof(size_t),
	sizeof(char *),
	sizeof(int),
	sizeof(string_list_t)
};

typedef struct config_spec {
	int type;
	int dimensions;
	const char *name;
	unsigned member_offset;
	unsigned member_size;
} config_spec_t;

// This looks silly, but we get in return an iterable array of configuration
// options complete with its default values...

#define member_size(type, member) sizeof(((type *)0)->member)

#define DECLARE_CONFIG_ARRAY(_dimensions_, _type_, _fullname_, _member_) \
	{ \
		.type = (_type_), \
		.dimensions = (_dimensions_), \
		.name = (_fullname_), \
		.member_offset = offsetof(rt_config_t, _member_), \
		.member_size = member_size(rt_config_t, _member_) \
	}

#define DECLARE_CONFIG(_type_, _fullname_, _member_) \
	DECLARE_CONFIG_ARRAY(0, _type_, _fullname_, _member_)

static config_spec_t config_spec_list[] = {
	DECLARE_CONFIG(TYPE_UINT64, "scheduler.quantum_ns", sched_quantum_ns),
	DECLARE_CONFIG(TYPE_UINT64, "scheduler.queue_batch", sched_batch_size),
	DECLARE_CONFIG(TYPE_UINT64, "scheduler.cpus_per_queue", sched_cpus_per_queue),
	DECLARE_CONFIG(TYPE_UINT64, "scheduler.in_queue_size", sched_in_queue_size),
	DECLARE_CONFIG(TYPE_BOOL, "scheduler.immediate_successor", sched_immediate_successor),
	DECLARE_CONFIG(TYPE_STR, "shared_memory.name", shm_name),
	DECLARE_CONFIG(TYPE_STR, "shared_memory.isolation_level", shm_isolation_level),
	DECLARE_CONFIG(TYPE_PTR, "shared_memory.start", shm_start),
	DECLARE_CONFIG(TYPE_SIZE, "shared_memory.size", shm_size),
	DECLARE_CONFIG(TYPE_STR,  "task_affinity.default", task_affinity_default),
	DECLARE_CONFIG(TYPE_STR,  "task_affinity.default_policy", task_affinity_default_policy),
	DECLARE_CONFIG(TYPE_BOOL, "thread_affinity.compat_support", thread_affinity_compat_support),
	DECLARE_CONFIG(TYPE_STR,  "topology.binding", topology_binding),
	DECLARE_CONFIG_ARRAY(1, TYPE_STR, "topology.numa_nodes", topology_numa_nodes),
	DECLARE_CONFIG_ARRAY(1, TYPE_STR, "topology.complex_sets", topology_complex_sets),
	DECLARE_CONFIG(TYPE_BOOL, "topology.print", topology_print),
	DECLARE_CONFIG(TYPE_BOOL, "debug.dump_config", debug_dump_config),
	DECLARE_CONFIG(TYPE_BOOL, "debug.print_binding", debug_print_binding),
	DECLARE_CONFIG(TYPE_STR, "governor.policy", governor_policy),
	DECLARE_CONFIG(TYPE_UINT64, "governor.spins", governor_spins),
	DECLARE_CONFIG(TYPE_BOOL, "hwcounters.verbose", hwcounters_verbose),
	DECLARE_CONFIG(TYPE_STR, "hwcounters.backend", hwcounters_backend),
	DECLARE_CONFIG(TYPE_LIST_STR, "hwcounters.papi_events", hwcounters_papi_events),
	DECLARE_CONFIG(TYPE_BOOL, "turbo.enabled", turbo_enabled),
	DECLARE_CONFIG(TYPE_BOOL, "monitoring.enabled", monitoring_enabled),
	DECLARE_CONFIG(TYPE_BOOL, "monitoring.verbose", monitoring_verbose),
	DECLARE_CONFIG(TYPE_STR, "instrumentation.version", instrumentation_version),
	DECLARE_CONFIG(TYPE_SIZE, "misc.stack_size", thread_stack_size),
	DECLARE_CONFIG(TYPE_UINT64, "ovni.level", ovni_level),
	DECLARE_CONFIG(TYPE_LIST_STR, "ovni.events", ovni_events),
	DECLARE_CONFIG(TYPE_SIZE, "ovni.kernel_ringsize", ovni_kernel_ringsize),
};

#endif // CONFIG_PARSE_H
