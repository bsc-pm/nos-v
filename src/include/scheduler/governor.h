/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef GOVERNOR_H
#define GOVERNOR_H

#include <stdatomic.h>
#include <stdint.h>

#include "compiler.h"
#include "defaults.h"
#include "scheduler/cpubitset.h"

enum governor_policy {
	BUSY = 0,
	IDLE = 1,
	HYBRID = 2
};

// Break circular dependency with dtlock types
struct dtlock_node;
struct delegation_lock;
typedef struct delegation_lock delegation_lock_t;

// Needed state for the power saving policy
typedef struct governor {
	cpu_bitset_t waiters;
	cpu_bitset_t sleepers;
	enum governor_policy policy;
	uint32_t hybrid_spins;
	uint32_t cpu_spin_counter[NR_CPUS];
} governor_t;

// Initializes the governor structures
__internal void governor_init(governor_t *ps);

// Notify that remaining waiters could not be scheduled a task
// Pass the dtlock as this may result in actions taken
__internal void governor_spin(governor_t *ps, delegation_lock_t *dtlock);

// Notify a waiter has been served
__internal void governor_waiter_served(governor_t *ps, const int waiter);

// Notify a sleeper has been woken up and served
__internal void governor_sleeper_served(governor_t *ps, const int sleeper);

// Request a CPU to be woken up either from waiters or sleepers, without any task
__internal void governor_wake_one(governor_t *ps, delegation_lock_t *dtlock);

// Notify a process has to shut down
__internal void governor_pid_shutdown(governor_t *ps, pid_t pid, delegation_lock_t *dtlock);

static inline cpu_bitset_t *governor_get_waiters(governor_t *ps)
{
	return &ps->waiters;
}

static inline cpu_bitset_t *governor_get_sleepers(governor_t *ps)
{
	return &ps->sleepers;
}

#endif // GOVERNOR_H
