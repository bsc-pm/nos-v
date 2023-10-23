/*
	This file is part of Nanos6 and nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2020-2023 Barcelona Supercomputing Center (BSC)
*/

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#include "compiler.h"

#include <stddef.h>
#include <stdint.h>

__internal void config_parse(void);
__internal void config_free(void);

/*
	Equivalences between config parser and rt_config_t:
		- TYPE_INT64 = int64_t
		- TYPE_PTR = void *
		- TYPE_UINT64 = uint64_t
		- TYPE_SIZE = size_t
		- TYPE_STR = const char *
		- TYPE_BOOL = int
*/

typedef struct string_list {
	char **strings;
	uint64_t num_strings;
} string_list_t;

typedef struct generic_array {
	void *items;
	uint64_t n;
} generic_array_t;

typedef struct rt_config {
	// Misc
	size_t thread_stack_size;

	// Shared Memory
	const char *shm_name;
	const char *shm_isolation_level;
	size_t shm_size;
	void *shm_start;

	// CPU Manager
	const char *cpumanager_binding;

	// Affinity
	const char *affinity_default;
	const char *affinity_default_policy;
	int affinity_compat_support;
	generic_array_t affinity_numa_nodes;

	// Scheduler
	uint64_t sched_cpus_per_queue;
	uint64_t sched_batch_size;
	uint64_t sched_quantum_ns;
	uint64_t sched_in_queue_size;
	int sched_immediate_successor;

	// Governor
	const char *governor_policy;
	uint64_t governor_spins;

	// Debug
	int debug_dump_config;

	// Hardware Counters
	int hwcounters_verbose;
	const char *hwcounters_backend;
	string_list_t hwcounters_papi_events;

	// Turbo
	int turbo_enabled;

	// Monitoring
	int monitoring_enabled;
	int monitoring_verbose;

	// Instrumentation
	const char *instrumentation_version;
} rt_config_t;

__internal extern rt_config_t nosv_config;

#endif // CONFIG_H
