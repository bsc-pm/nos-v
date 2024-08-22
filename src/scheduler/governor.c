/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include "config/config.h"
#include "hardware/cpus.h"
#include "scheduler/dtlock.h"
#include "scheduler/governor.h"

void governor_init(governor_t *governor)
{
	int actual_cpus = cpus_count();

	cpu_bitset_init(&governor->sleepers, actual_cpus);
	cpu_bitset_init(&governor->waiters, actual_cpus);

	for (int i = 0; i < actual_cpus; ++i) {
		governor->cpu_spin_counter[i] = 0;
	}

	// The governor policy only affects the number of spins
	assert(nosv_config.governor_policy);
	if (strcmp(nosv_config.governor_policy, "hybrid") == 0) {
		governor->spins = nosv_config.governor_spins;
	} else if (strcmp(nosv_config.governor_policy, "idle") == 0) {
		// In the idle policy we follow the same code paths as HYBRID but hybrid_spins = 0
		governor->spins = 0;
	} else {
		assert(strcmp(nosv_config.governor_policy, "busy") == 0);
		governor->spins = UINT64_MAX;
	}
}

void governor_free(__maybe_unused governor_t *governor)
{
	// Nothing to do for now
}

static inline void governor_sleep_cpu(governor_t *governor, const int waiter, delegation_lock_t *dtlock)
{
	// Tell the waiter to sleep
	dtlock_serve(dtlock, waiter, NULL, 0, DTLOCK_SIGNAL_SLEEP);
	// Remove it from the waiter mask
	governor_served(governor, waiter);
	// Add it to the sleepers mask
	cpu_bitset_set(&governor->sleepers, waiter);
}

void governor_apply_policy(governor_t *governor, delegation_lock_t *dtlock)
{
	int cpu = 0;
	CPU_BITSET_FOREACH(&governor->waiters, cpu) {
		uint64_t *cpu_spins = &governor->cpu_spin_counter[cpu];

		if (!dtlock_is_cpu_blockable(dtlock, cpu)) {
			dtlock_serve(dtlock, cpu, NULL, 0, DTLOCK_SIGNAL_DEFAULT);
			governor_served(governor, cpu);
		} else if (++(*cpu_spins) > governor->spins) {
			governor_sleep_cpu(governor, cpu, dtlock);
		}
	}
}

// Request a CPU to be woken up either from waiters or sleepers
void governor_wake_one(governor_t *governor, delegation_lock_t *dtlock)
{
	int candidate = cpu_bitset_ffs(&governor->waiters);
	if (candidate >= 0) {
		dtlock_serve(dtlock, candidate, DTLOCK_ITEM_RETRY, 0, DTLOCK_SIGNAL_DEFAULT);
		governor_served(governor, candidate);
		return;
	}

	candidate = cpu_bitset_ffs(&governor->sleepers);
	if (candidate >= 0) {
		dtlock_serve(dtlock, candidate, DTLOCK_ITEM_RETRY, 0, DTLOCK_SIGNAL_WAKE);
		governor_served(governor, candidate);
	}
}

int governor_served(governor_t *governor, const int cpu)
{
	if (cpu_bitset_isset(&governor->waiters, cpu)) {
		cpu_bitset_clear(&governor->waiters, cpu);
		governor->cpu_spin_counter[cpu] = 0;
		return 0;
	} else {
		cpu_bitset_clear(&governor->sleepers, cpu);
		return 1;
	}
}

void governor_shutdown_process(governor_t *governor, pid_t pid, delegation_lock_t *dtlock)
{
	int cpu;

	CPU_BITSET_FOREACH(&governor->waiters, cpu) {
		if (cpu_get_pid(cpu) == pid) {
			// Wake up the waiter with a NULL to get it to check if it has to shutdown
			dtlock_serve(dtlock, cpu, NULL, 0, DTLOCK_SIGNAL_DEFAULT);
			governor_served(governor, cpu);
		}
	}

	CPU_BITSET_FOREACH(&governor->sleepers, cpu) {
		if (cpu_get_pid(cpu) == pid) {
			// Wake up the sleeper with a NULL to get it to check if it has to shutdown
			dtlock_serve(dtlock, cpu, NULL, 0, DTLOCK_SIGNAL_WAKE);
			governor_served(governor, cpu);
		}
	}
}

int governor_update_cpumasks(governor_t *governor, delegation_lock_t *dtlock)
{
	int nwaiters = dtlock_update_waiters(dtlock, &governor->waiters);
	int nsleepers = cpu_bitset_count(&governor->sleepers);
	return nwaiters + nsleepers;
}
