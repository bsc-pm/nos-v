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
	pid_structures_t *local = salloc(sizeof(pid_structures_t), cpu_get_current());
	threadmanager_init(&local->threadmanager);

	// While holding this lock, we have to check if there are free CPUs that we have to occupy
	cpu_t *free_cpu;
	free_cpu = cpu_pop_free();

	while(free_cpu) {
		worker_create(free_cpu);
		free_cpu = cpu_pop_free(logical_pid);
	}

	nosv_mutex_destroy(&pidmanager->lock);
}

void pidmanager_shutdown()
{

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