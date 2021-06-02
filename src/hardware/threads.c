/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>

#include "common.h"
#include "hardware/cpus.h"
#include "hardware/threads.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"

thread_local nosv_worker_t *current_worker;

void threadmanager_init(thread_manager_t *threadmanager)
{
	nosv_spin_init(&threadmanager->idle_spinlock);
	nosv_spin_init(&threadmanager->shutdown_spinlock);
	list_init(&threadmanager->idle_threads);
	list_init(&threadmanager->shutdown_threads);
}

void *worker_start_routine(void *arg)
{
	current_worker = (nosv_worker_t *)arg;
	cpu_set_current(current_worker->cpu->logic_id);

	return NULL;
}

nosv_worker_t *worker_create(cpu_t *cpu)
{
	int ret;

	nosv_worker_t *worker = (nosv_worker_t *) salloc(sizeof(nosv_worker_t), cpu_get_current());
	worker->cpu = cpu;

	pthread_attr_t attr;
	ret = pthread_attr_init(&attr);
	if (ret)
		nosv_abort("Cannot create pthread attributes");

	pthread_attr_setaffinity_np(&attr, sizeof(cpu->cpuset), &cpu->cpuset);

	ret = pthread_create(&worker->kthread, &attr, worker_start_routine, worker);
	if (ret)
		nosv_abort("Cannot create pthread");

	return worker;
}

void worker_join(nosv_worker_t *worker)
{
	int ret = pthread_join(worker->kthread, NULL);
	if (ret)
		nosv_abort("Cannot join pthread");
}
