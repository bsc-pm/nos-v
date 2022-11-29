/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "config/config.h"
#include "hardware/cpus.h"
#include "hardware/pids.h"
#include "memory/sharedmemory.h"
#include "memory/backbone.h"
#include "memory/slab.h"
#include "monitoring/monitoring.h"
#include "scheduler/scheduler.h"

// Fix for older kernels
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE MAP_FIXED
#endif

static_smem_config_t st_config;
static int pid_slot_config = -1;

// Protect file locks with acquire-release semantics
static inline int shm_lock(int fd)
{
	int ret = flock(fd, LOCK_EX);
	atomic_thread_fence(memory_order_acquire);
	return ret;
}

static inline int shm_unlock(int fd)
{
	atomic_thread_fence(memory_order_release);
	return flock(fd, LOCK_UN);
}

static void smem_config_initialize(smem_config_t *config)
{
	memset(config->processes, 0, sizeof(process_identifier_t) * MAX_PIDS);
	config->processes[0] = get_process_self();
	assert(config->processes[0].pid);
	config->cpumanager_ptr = NULL;
	config->scheduler_ptr = NULL;
	config->pidmanager_ptr = NULL;
	config->monitoring_ptr = NULL;
	nosv_mutex_init(&config->mutex);
	config->count = 0;
	memset(config->per_pid_structures, 0, MAX_PIDS * sizeof(void *));
}

// Boostrap of the shared memory for the first process
static void smem_initialize_first(void)
{
	pid_slot_config = 0;
	smem_config_initialize(st_config.config);
	backbone_alloc_init(((char *)nosv_config.shm_start) + sizeof(smem_config_t), nosv_config.shm_size - sizeof(smem_config_t), 1);
	slab_init();
	cpus_init(1);
	pidmanager_init(1);
	scheduler_init(1);
	monitoring_init(1);
}

// Bootstrap for the rest of processes
static void smem_initialize_rest(void)
{
	// First of all, register in PID list
	for (int i = 0; i < MAX_PIDS; ++i) {
		if (!st_config.config->processes[i].pid) {
			pid_slot_config = i;
			st_config.config->processes[i] = get_process_self();
			assert(st_config.config->processes[i].pid);
			break;
		}
	}

	if (pid_slot_config < 0)
		nosv_abort("Maximum number of concurrent nOS-V processes surpassed");

	backbone_alloc_init(((char *)nosv_config.shm_start) + sizeof(smem_config_t), nosv_config.shm_size - sizeof(smem_config_t), 0);
	cpus_init(0);
	pidmanager_init(0);
	scheduler_init(0);
	monitoring_init(0);
}

static inline int check_processes_correct(void)
{
	pid_t pid;
	process_identifier_t pi;

	for (int i = 0; i < MAX_PIDS; ++i) {
		pid = st_config.config->processes[i].pid;
		if (pid) {
			pi = get_process(pid);
			if (pi.pid < 0)
				return 0;

			assert(pi.pid == pid);
			if (pi.start_time != st_config.config->processes[i].start_time)
				return 0;
		}
	}

	return 1;
}

static inline void calculate_shared_memory_permissions(void)
{
	int ret = 0;

	if (!strcmp(nosv_config.shm_isolation_level, "process")) {
		st_config.smem_mode = 0600;
		ret = nosv_asprintf((char **) &st_config.smem_name, "%s-u%ld-p%ld", nosv_config.shm_name, (long) geteuid(), (long) getpid());
	} else if (!strcmp(nosv_config.shm_isolation_level, "user")) {
		st_config.smem_mode = 0600;
		ret = nosv_asprintf((char **) &st_config.smem_name, "%s-u%ld", nosv_config.shm_name, (long) geteuid());
	} else if (!strcmp(nosv_config.shm_isolation_level, "group")) {
		st_config.smem_mode = 0660;
		ret = nosv_asprintf((char **) &st_config.smem_name, "%s-g%ld", nosv_config.shm_name, (long) getegid());
	} else if (!strcmp(nosv_config.shm_isolation_level, "public")) {
		st_config.smem_mode = 0666;
		ret = nosv_asprintf((char **) &st_config.smem_name, "%s", nosv_config.shm_name);
	} else {
		// Allegedly unreachable condition
		nosv_abort("Unknown isolation level!");
	}

	if (ret)
		nosv_abort("Error determining shared memory name");
}

static void segment_create(void)
{
	int ret;
	struct stat st;

	calculate_shared_memory_permissions();

	st_config.smem_fd = 0;
	while (!st_config.smem_fd) {
		// Reset the umask as we are correctly setting the file permissions
		mode_t old_umask = umask(0000);
		st_config.smem_fd = shm_open(st_config.smem_name, O_CREAT | O_RDWR, st_config.smem_mode);
		umask(old_umask);

		if (st_config.smem_fd < 0)
			nosv_abort("Cannot open shared memory segment");

		// Synchronization in the first stages of the shared memory is done through file locks
		ret = shm_lock(st_config.smem_fd);
		if (ret)
			nosv_abort("Cannot grab initial file lock");

		ret = fstat(st_config.smem_fd, &st);
		if (ret)
			nosv_abort("Cannot stat shared memory segment");

		if (st.st_nlink == 0) {
			// We raced with another process that was unlinking the shared memory segment
			// EAGAIN basically

			shm_unlock(st_config.smem_fd);
			close(st_config.smem_fd);
			st_config.smem_fd = 0;
		} else {
			st_config.config = NULL;

			if (st.st_size != 0) {
				// Pre-initialized segment
				assert(st.st_size == nosv_config.shm_size);
				st_config.config = (smem_config_t *)mmap(nosv_config.shm_start, nosv_config.shm_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, st_config.smem_fd, 0);
				if (st_config.config == MAP_FAILED)
					nosv_abort("Cannot map shared memory");

				if (!check_processes_correct()) {
					// One or more of the processes has crashed.
					// Unlink the segment and retry
					nosv_warn("Detected stale shared memory");

					ret = munmap(nosv_config.shm_start, nosv_config.shm_size);
					if (ret)
						nosv_abort("Cannot unmap shared memory");

					ret = shm_unlink(st_config.smem_name);
					if (ret)
						nosv_abort("Cannot unlink shared memory");

					shm_unlock(st_config.smem_fd);
					close(st_config.smem_fd);
					st_config.smem_fd = 0;
				} else {
					smem_initialize_rest();
				}
			} else {
				ftruncate(st_config.smem_fd, (off_t) nosv_config.shm_size);
				st_config.config = (smem_config_t *)mmap(nosv_config.shm_start, nosv_config.shm_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, st_config.smem_fd, 0);
				if (st_config.config == MAP_FAILED)
					nosv_abort("Cannot map shared memory");

				// Initialize everything
				smem_initialize_first();
			}
		}
	}

	st_config.config->count++;

	// Release lock
	ret = shm_unlock(st_config.smem_fd);
	if (ret)
		nosv_abort("Cannot release initial file lock");
}

static void segment_unregister_last(void)
{
	// NOTE: This is called from segment_unregister by the last process to
	// arrive at the shutdown

	monitoring_shutdown();
}

static void segment_unregister(void)
{
	int ret;

	ret = shm_lock(st_config.smem_fd);
	if (ret)
		nosv_abort("Cannot grab unregister file lock");

	int cnt = --(st_config.config->count);
	if (!cnt) {
		// Before unmaping memory, if this is the last process, shutdown every
		// module that has to be shutdown only once (by the last process)
		segment_unregister_last();
	}

	st_config.config->processes[pid_slot_config].pid = 0;

	ret = munmap(nosv_config.shm_start, nosv_config.shm_size);
	if (ret)
		nosv_warn("Cannot unmap shared memory");

	if (!cnt) {
		ret = shm_unlink(st_config.smem_name);
		if (ret)
			nosv_warn("Cannot unlink shared memory");
	}

	ret = shm_unlock(st_config.smem_fd);
	if (ret)
		nosv_warn("Cannot release final file lock");

	ret = close(st_config.smem_fd);
	if (ret)
		nosv_warn("Cannot close memory segment");
}

void smem_initialize(void)
{
	segment_create();
}

void smem_shutdown(void)
{
	segment_unregister();
	free((void *)st_config.smem_name);
}
