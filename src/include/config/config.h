/*
	This file is part of Nanos6 and nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2020-2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef CONFIG_H
#define CONFIG_H

#include "compiler.h"

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

typedef struct rt_config {
	// Shared Memory
	const char *shm_name;
	size_t shm_size;
	void *shm_start;

	// Scheduler
	uint64_t sched_cpus_per_queue;
	uint64_t sched_batch_size;
	uint64_t sched_quantum_ns;
	uint64_t sched_in_queue_size;

	// Debug
	int debug_dump_config;
} rt_config_t;

__internal extern rt_config_t nosv_config;

#endif // CONFIG_H
