/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022Barcelona Supercomputing Center (BSC)
*/

#ifndef GOVERNOR_H
#define GOVERNOR_H

#include <stdatomic.h>
#include <stdint.h>

#include "compiler.h"
#include "defaults.h"
#include "scheduler/cpubitset.h"
#include "scheduler/dtlock.h"

enum governor_policy {
	BUSY = 0,
	IDLE = 1,
	HYBRID = 2
};

// Needed state for the power saving policy
typedef struct governor {
	cpu_bitset_t waiters;
	cpu_bitset_t sleepers;
	uint64_t spins;
	uint64_t cpu_spin_counter[NR_CPUS];
} governor_t;

// Initializes the governor structures
__internal void governor_init(governor_t *governor);

// Notify that remaining waiters could not be scheduled a task
// Pass the dtlock as this may result in actions taken
__internal void governor_apply_policy(governor_t *governor, delegation_lock_t *dtlock);

// Notify a CPU has been served
// Returns 1 if the CPU was sleeping (and thus needs to be woken)
__internal int governor_served(governor_t *governor, const int cpu);

// Request a CPU to be woken up either from waiters or sleepers, without any task
__internal void governor_wake_one(governor_t *governor, delegation_lock_t *dtlock);

// Notify a process has to shut down
__internal void governor_pid_shutdown(governor_t *governor, pid_t pid, delegation_lock_t *dtlock);

static inline cpu_bitset_t *governor_get_waiters(governor_t *governor)
{
	return &governor->waiters;
}

static inline cpu_bitset_t *governor_get_sleepers(governor_t *governor)
{
	return &governor->sleepers;
}

#endif // GOVERNOR_H
