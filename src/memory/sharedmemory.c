/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "memory/sharedmemory.h"

static_smem_config_t st_config;

static void smem_config_initialize(smem_config_t *config)
{
	config->alloc_ptr = NULL;
	config->scheduler_ptr = NULL;
	nosv_mutex_init(&config->mutex);
	config->count = 0;
}

static void segment_create()
{
	int ret;
	struct stat st;

	st_config.smem_fd = 0;

	while (!st_config.smem_fd) {
		st_config.smem_fd = shm_open(SMEM_NAME, O_CREAT | O_RDWR, 0644);

		if (st_config.smem_fd < 0)
			nosv_abort("Cannot open shared memory segment");

		// Synchronization in the first stages of the shared memory is done through file locks
		ret = flock(st_config.smem_fd, LOCK_EX);
		if (ret)
			nosv_abort("Cannot grab initial file lock");

		ret = fstat(st_config.smem_fd, &st);
		if (ret)
			nosv_abort("Cannot stat shared memory segment");

		if (st.st_nlink == 0) {
			// We raced with another process that was unlinking the shared memory segment
			// EAGAIN basically

			flock(st_config.smem_fd, LOCK_UN);
			close(st_config.smem_fd);
			st_config.smem_fd = 0;
		}
	}

	st_config.config = NULL;

	if (st.st_size != 0) {
		// Pre-initialized segment
		assert(st.st_size == SMEM_SIZE);
		st_config.config = (smem_config_t *)mmap(SMEM_START_ADDR, SMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, st_config.smem_fd, 0);
		if (st_config.config == MAP_FAILED)
			nosv_abort("Cannot map shared memory");
	} else {
		ftruncate(st_config.smem_fd, SMEM_SIZE);
		st_config.config = (smem_config_t *)mmap(SMEM_START_ADDR, SMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, st_config.smem_fd, 0);
		if (st_config.config == MAP_FAILED)
			nosv_abort("Cannot map shared memory");

		// Initialize everything
		smem_config_initialize(st_config.config);
	}

	st_config.config->count++;

	// Release lock
	ret = flock(st_config.smem_fd, LOCK_UN);
	if (ret)
		nosv_abort("Cannot release initial file lock");
}

static void segment_unregister()
{
	int ret;

	ret = flock(st_config.smem_fd, LOCK_EX);
	if (ret)
		nosv_abort("Cannot grab unregister file lock");

	int cnt = --(st_config.config->count);

	ret = munmap(SMEM_START_ADDR, SMEM_SIZE);
	if (ret)
		nosv_warn("Cannot unmap shared memory");

	if (!cnt) {
		ret = shm_unlink(SMEM_NAME);
		if (ret)
			nosv_warn("Cannot unlink shared memory");
	}

	ret = flock(st_config.smem_fd, LOCK_UN);
	if (ret)
		nosv_warn("Cannot release final file lock");

	ret = close(st_config.smem_fd);
	if (ret)
		nosv_warn("Cannot close memory segment");

}

void smem_initialize()
{
	segment_create();
}

void smem_shutdown()
{
	segment_unregister();
}
