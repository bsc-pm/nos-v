/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include "compiler.h"
#include "climits.h"
#include "generic/mutex.h"
#include "generic/proc.h"

#define SMEM_START_ADDR ((void *) 0x0000200000000000)
// #define SMEM_SIZE (1ULL << 21)
#define SMEM_SIZE (1ULL << 31)
#define SMEM_NAME "nosv"

__internal void smem_initialize(void);

__internal void smem_shutdown(void);

typedef struct smem_config {
	// Process Identifiers
	process_identifier_t processes[MAX_PIDS];
	nosv_mutex_t mutex;
	void *scheduler_ptr;
	void *cpumanager_ptr;
	void *pidmanager_ptr;
	int count;
	void *per_pid_structures[MAX_PIDS];
} smem_config_t;

typedef struct static_smem_config {
	smem_config_t *config;
	int smem_fd;
} static_smem_config_t;

__internal extern static_smem_config_t st_config;

#endif // SHARED_MEMORY_H
