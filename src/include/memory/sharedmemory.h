/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include "compiler.h"
#include "mutex.h"

#define SMEM_START_ADDR 0x2000000000000000
#define SMEM_SIZE (1 << 21)

__hidden void smem_initialize();

__hidden void smem_shutdown();

typedef struct smem_config {
	void *scheduler_ptr;
	void *alloc_ptr;
	nosv_mutex_t mutex;
} smem_config_t;

#endif // SHARED_MEMORY_H