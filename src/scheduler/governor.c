/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include "config/config.h"
#include "hardware/cpus.h"
#include "scheduler/dtlock.h"
#include "scheduler/governor.h"

void governor_init(governor_t *ps)
{
	int actual_cpus = cpus_count();

	cpu_bitset_init(&ps->sleepers, actual_cpus);
	cpu_bitset_init(&ps->waiters, actual_cpus);

	for (int i = 0; i < actual_cpus; ++i) {
		ps->cpu_spin_counter[i] = 0;
	}

	assert(nosv_config.governor_policy);
	if (strcmp(nosv_config.governor_policy, "hybrid") == 0) {
		ps->policy = HYBRID;
		ps->hybrid_spins = nosv_config.governor_spins;
	} else if (strcmp(nosv_config.governor_policy, "idle") == 0) {
		// In the idle policy we follow the same code paths as HYBRID but hybrid_spins = 0
		ps->policy = IDLE;
		ps->hybrid_spins = 0;
	} else {
		assert(strcmp(nosv_config.governor_policy, "busy") == 0);
		ps->policy = BUSY;
		ps->hybrid_spins = 0;
	}
}

static inline void governor_bedtime(governor_t *ps, const int waiter, delegation_lock_t *dtlock)
{
	// Tell the waiter to sleep
	dtlock_serve(dtlock, waiter, NULL, DTLOCK_SIGNAL_SLEEP);
	// Remove it from the waiter mask
	governor_waiter_served(ps, waiter);
	// Add it to the sleepers mask
	cpu_bitset_set(&ps->sleepers, waiter);
}

void governor_spin(governor_t *ps, delegation_lock_t *dtlock)
{
	// This matches with the release on dtlock->item which sets the flags for each access.
	atomic_thread_fence(memory_order_acquire);

	int cpu = 0;
	if (ps->policy == BUSY) {
		// Every waiter is spinning
		CPU_BITSET_FOREACH(&ps->waiters, cpu) {
			if (!dtlock_blocking(dtlock, cpu)) {
				dtlock_serve(dtlock, cpu, NULL, DTLOCK_SIGNAL_DEFAULT);
				governor_waiter_served(ps, cpu);
			}
		}
	} else {
		// Every waiter is spinning
		CPU_BITSET_FOREACH(&ps->waiters, cpu) {
			if (!dtlock_blocking(dtlock, cpu)) {
				dtlock_serve(dtlock, cpu, NULL, DTLOCK_SIGNAL_DEFAULT);
				governor_waiter_served(ps, cpu);
			} else if (++(ps->cpu_spin_counter[cpu]) > ps->hybrid_spins) {
				governor_bedtime(ps, cpu, dtlock);
			}
		}
	}
}

// Request a CPU to be woken up either from waiters or sleepers
void governor_wake_one(governor_t *ps, delegation_lock_t *dtlock)
{
	int candidate = cpu_bitset_ffs(&ps->waiters);
	if (candidate >= 0) {
		dtlock_serve(dtlock, candidate, ITEM_DTLOCK_EAGAIN, DTLOCK_SIGNAL_DEFAULT);
		governor_waiter_served(ps, candidate);
		return;
	}

	candidate = cpu_bitset_ffs(&ps->sleepers);
	if (candidate >= 0) {
		dtlock_serve(dtlock, candidate, ITEM_DTLOCK_EAGAIN, DTLOCK_SIGNAL_WAKE);
		governor_sleeper_served(ps, candidate);
	}
}

void governor_waiter_served(governor_t *ps, const int waiter)
{
	cpu_bitset_clear(&ps->waiters, waiter);
	ps->cpu_spin_counter[waiter] = 0;
}

void governor_sleeper_served(governor_t *ps, const int sleeper)
{
	cpu_bitset_clear(&ps->sleepers, sleeper);
}

void governor_pid_shutdown(governor_t *ps, pid_t pid, delegation_lock_t *dtlock)
{
	int cpu;

	CPU_BITSET_FOREACH(&ps->waiters, cpu) {
		if (cpu_get_pid(cpu) == pid) {
			// Wake up the waiter with a NULL to get it to check if it has to shutdown
			dtlock_serve(dtlock, cpu, NULL, DTLOCK_SIGNAL_DEFAULT);
			governor_waiter_served(ps, cpu);
		}
	}

	CPU_BITSET_FOREACH(&ps->sleepers, cpu) {
		if (cpu_get_pid(cpu) == pid) {
			// Wake up the sleeper with a NULL to get it to check if it has to shutdown
			dtlock_serve(dtlock, cpu, NULL, DTLOCK_SIGNAL_WAKE);
			governor_sleeper_served(ps, cpu);
		}
	}
}
