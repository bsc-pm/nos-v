/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>

#include "common.h"
#include "compiler.h"
#include "hardware/pids.h"
#include "hardware/cpus.h"
#include "hwcounters/hwcounters.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"

#define PID_STR(a) st_config.config->per_pid_structures[a]

__internal pid_manager_t *pidmanager;

__internal int logic_pid;
__internal pid_t system_pid;

void pidmanager_register(void)
{
	pid_bitset_t set;
	BIT_FILL(MAX_PIDS, &set);
	system_pid = getpid();

	nosv_mutex_lock(&pidmanager->lock);

	// Xor with all ones to get the "not set" bits to be one.
	BIT_XOR(MAX_PIDS, &set, &pidmanager->pids_alloc);
	// Now find the first set bit, being the first free logical PID
	// Its a 1-index, so we have to substract one
	logic_pid = BIT_FFS(MAX_PIDS, &set) - 1;

	if (logic_pid == -1)
		nosv_abort("Maximum number of concurrent nOS-V processes surpassed");

	// Set both as PID allocated and as ready
	BIT_SET(MAX_PIDS, logic_pid, &pidmanager->pids_alloc);
	BIT_SET(MAX_PIDS, logic_pid, &pidmanager->pids);

	// Initialize PID-local structures
	pid_structures_t *local = (pid_structures_t *) salloc(sizeof(pid_structures_t), cpu_get_current());
	threadmanager_init(&local->threadmanager);
	PID_STR(logic_pid) = local;

	// While holding this lock, we have to check if there are free CPUs that we have to occupy
	cpu_t *free_cpu;
	free_cpu = cpu_pop_free(logic_pid);
	task_execution_handle_t handle = EMPTY_TASK_EXECUTION_HANDLE;

	while (free_cpu) {
		worker_create_local(&local->threadmanager, free_cpu, handle);
		free_cpu = cpu_pop_free(logic_pid);
	}

	nosv_mutex_unlock(&pidmanager->lock);
}

void pidmanager_shutdown(void)
{
	nosv_mutex_lock(&pidmanager->lock);

	// Unregister this process, and make it available
	BIT_CLR(MAX_PIDS, logic_pid, &pidmanager->pids);

	pid_structures_t *local = (pid_structures_t *)PID_STR(logic_pid);
	assert(local);
	PID_STR(logic_pid) = NULL;

	nosv_mutex_unlock(&pidmanager->lock);

	// Notify all threads they have to shut down and wait until they do
	// Each thread will pass its CPU during the shutdown process
	threadmanager_shutdown(&local->threadmanager);

	// Now deallocate the PID. We do this in two phases to prevent an ABA problem
	// with logical PIDs
	nosv_mutex_lock(&pidmanager->lock);
	BIT_CLR(MAX_PIDS, logic_pid, &pidmanager->pids_alloc);
	nosv_mutex_unlock(&pidmanager->lock);
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
	BIT_ZERO(MAX_PIDS, &pidmanager->pids_alloc);
	st_config.config->pidmanager_ptr = pidmanager;
}

thread_manager_t *pidmanager_get_threadmanager(int pid)
{
	return &((pid_structures_t *)PID_STR(pid))->threadmanager;
}

void pidmanager_transfer_to_idle(cpu_t *cpu)
{
	// Find an active PID and transfer the CPU there
	// Called on shutdown of a process

	nosv_mutex_lock(&pidmanager->lock);
	int pid = BIT_FFS(MAX_PIDS, &pidmanager->pids) - 1;

	assert(pid != logic_pid);


	// Unconditionally reset our affinity during shutdown.
	cpu_affinity_reset();

	if (pid >= 0) {
		// Wake remote CPU
		task_execution_handle_t handle = EMPTY_TASK_EXECUTION_HANDLE;
		cpu_transfer(pid, cpu, handle);
	} else {
		cpu_mark_free(cpu);
	}

	nosv_mutex_unlock(&pidmanager->lock);
}
