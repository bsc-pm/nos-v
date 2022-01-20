/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef ARBITER_H
#define ARBITER_H

#include "compiler.h"
#include "nosv.h"
#include "scheduler/dtlock.h"
#include "scheduler/governor.h"

typedef struct arbiter {
	delegation_lock_t dtlock;
	governor_t governor;
} arbiter_t;

// Initialize internal structures of the arbiter
__internal void arbiter_init(arbiter_t *arbiter);

// Enter the scheduler through the arbiter
// Returns 0 if the thread is now the server, 1 if a task was served
__internal int arbiter_enter(arbiter_t *arbiter, int cpu, int blocking, nosv_task_t *task);

// Exit the scheduler through the arbiter
__internal void arbiter_exit(arbiter_t *arbiter, int server);

// Serve a task to a CPU
__internal void arbiter_serve(arbiter_t *arbiter, nosv_task_t task, int cpu);

// Get the masks of pending CPUs
__internal int arbiter_process_pending(arbiter_t *arbiter, int spin);

// Get bitsets for waiters and sleepers
static inline void arbiter_get_cpumasks(arbiter_t *arbiter, cpu_bitset_t **waiters, cpu_bitset_t **sleepers)
{
	*waiters = governor_get_waiters(&arbiter->governor);
	*sleepers = governor_get_sleepers(&arbiter->governor);
}

// Try to enter the arbiter
static inline int arbiter_try_enter(arbiter_t *arbiter)
{
	return dtlock_try_lock(&arbiter->dtlock);
}

// Notify PID shutdown
__internal void arbiter_shutdown_process(arbiter_t *arbiter, int pid);

#endif
