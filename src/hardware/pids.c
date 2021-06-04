/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>

#include "common.h"
#include "hardware/pids.h"
#include "hardware/cpus.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"

#define PID_STR(a) st_config.config->per_pid_structures[a]

pid_manager_t *pidmanager;

int logical_pid;
pid_t system_pid;

void pidmanager_register()
{
	pid_bitset_t set;
	BIT_FILL(MAX_PIDS, &set);
	system_pid = getpid();

	nosv_mutex_lock(&pidmanager->lock);

	// Xor with all ones to get the "not set" bits to be one.
	BIT_XOR(MAX_PIDS, &set, &pidmanager->pids);
	// Now find the first set bit, being the first free logical PID
	// Its a 1-index, so we have to substract one
	logical_pid = BIT_FFS(MAX_PIDS, &set) - 1;

	if (logical_pid == -1)
		nosv_abort("Maximum number of concurrent nOS-V processes surpassed");

	BIT_SET(MAX_PIDS, logical_pid, &pidmanager->pids);

	// Initialize PID-local structures
	pid_structures_t *local = (pid_structures_t *) salloc(sizeof(pid_structures_t), cpu_get_current());
	threadmanager_init(&local->threadmanager);
	PID_STR(logical_pid) = local;

	// While holding this lock, we have to check if there are free CPUs that we have to occupy
	cpu_t *free_cpu;
	free_cpu = cpu_pop_free(logical_pid);

	while(free_cpu) {
		worker_create_local(&local->threadmanager, free_cpu, NULL);
		free_cpu = cpu_pop_free(logical_pid);
	}

	nosv_mutex_unlock(&pidmanager->lock);
}

void pidmanager_shutdown()
{
	nosv_mutex_lock(&pidmanager->lock);

	// Unregister this process, and make it available
	BIT_CLR(MAX_PIDS, logical_pid, &pidmanager->pids);

	pid_structures_t *local = (pid_structures_t *)PID_STR(logical_pid);
	assert(local);
	PID_STR(logical_pid) = NULL;

	nosv_mutex_unlock(&pidmanager->lock);

	// Notify all threads they have to shut down and wait until they do
	// Each thread will pass its CPU during the shutdown process
	threadmanager_shutdown(&local->threadmanager);
}

void pidmanager_init(int initialize)
{
	if (!initialize) {
		pidmanager = st_config.config->pidmanager_ptr;
		assert(pidmanager);
		return;
	}

	pidmanager = (pid_manager_t *)salloc(sizeof(pid_manager_t), 0);
	nosv_mutex_init(&pidmanager->lock);
	BIT_ZERO(MAX_PIDS, &pidmanager->pids);
	st_config.config->pidmanager_ptr = pidmanager;
}

thread_manager_t *pidmanager_get_threadmanager(int pid)
{
	return &((pid_structures_t *)PID_STR(pid))->threadmanager;
}