/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2022 Barcelona Supercomputing Center (BSC)
*/


#include "hardware/cpus.h"
#include "scheduler/arbiter.h"

void arbiter_init(arbiter_t *arbiter)
{
	int cpu_count = cpus_count();

	dtlock_init(&arbiter->dtlock, cpu_count * 2);
	governor_init(&arbiter->governor);
}

int arbiter_enter(arbiter_t *arbiter, int cpu, int blocking, nosv_task_t *task)
{
	return dtlock_lock_or_delegate(&arbiter->dtlock, (uint64_t)cpu, (void **)task, blocking);
}

void arbiter_exit(arbiter_t *arbiter, int server)
{
	// If the delegation lock is empty, ensure that the cycle continues by waking a thread
	// This check is racy, but it doesn't matter
	if (server && dtlock_empty(&arbiter->dtlock))
		governor_wake_one(&arbiter->governor, &arbiter->dtlock);

	dtlock_unlock(&arbiter->dtlock);
}

void arbiter_serve(arbiter_t *arbiter, nosv_task_t task, int cpu)
{
	// Notify we're serving a CPU
	// If this condition is true, we have to wake it up
	if (governor_served(&arbiter->governor, cpu))
		dtlock_serve(&arbiter->dtlock, cpu, task, DTLOCK_SIGNAL_WAKE);
	else
		dtlock_serve(&arbiter->dtlock, cpu, task, DTLOCK_SIGNAL_DEFAULT);
}

int arbiter_process_pending(arbiter_t *arbiter, int spin)
{
	int ret = dtlock_update_waiters(&arbiter->dtlock, governor_get_waiters(&arbiter->governor));

	if (spin)
		governor_apply_policy(&arbiter->governor, &arbiter->dtlock);

	return ret || cpu_bitset_count(governor_get_sleepers(&arbiter->governor));
}

void arbiter_shutdown_process(arbiter_t *arbiter, int pid)
{
	governor_pid_shutdown(&arbiter->governor, pid, &arbiter->dtlock);
}
