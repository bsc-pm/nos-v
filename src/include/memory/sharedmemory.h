/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include "compiler.h"
#include "mutex.h"

#define SMEM_START_ADDR ((void *) 0x0000200000000000)
// #define SMEM_SIZE (1 << 21)
#define SMEM_SIZE (1 << 27)
#define SMEM_NAME "nosv"

void smem_initialize();

void smem_shutdown();

typedef struct smem_config {
	nosv_mutex_t mutex;
	void *scheduler_ptr;
	void *alloc_ptr;
	int count;
} smem_config_t;

typedef struct static_smem_config {
	smem_config_t *config;
	int smem_fd;
} static_smem_config_t;

extern static_smem_config_t st_config;

#endif // SHARED_MEMORY_H