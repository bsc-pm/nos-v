/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef PID_STRUCTURES_H
#define PID_STRUCTURES_H

#include <unistd.h>

#include "hardware/threads.h"
#include "hardware/topology.h"

typedef struct pid_structures {
	thread_manager_t threadmanager;
} pid_structures_t;

BITSET_DEFINE(pid_bitset, MAX_PIDS)

typedef struct pid_bitset pid_bitset_t;

typedef struct pid_manager {
	nosv_sys_mutex_t lock;
	pid_bitset_t pids;
	pid_bitset_t pids_alloc;
} pid_manager_t;

__internal extern int logic_pid;
__internal extern pid_t system_pid;

__internal void pidmanager_init(int initialize);
__internal thread_manager_t *pidmanager_get_threadmanager(int pid);
__internal void pidmanager_transfer_to_idle(cpu_t *cpu);

__internal void pidmanager_register(void);
__internal void pidmanager_unregister(void);
__internal void pidmanager_shutdown(void);
__internal void pidmanager_free(void);

#endif // PID_STRUCTURES_H
