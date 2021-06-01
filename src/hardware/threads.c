/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include "common.h"
#include "hardware/cpus.h"
#include "hardware/threads.h"
#include "memory/slab.h"


int threadmanager_init()
{
	int cpus = cpus_count();

	for (int i = 0; i < cpus; ++i)
		worker_create(cpu_get(i));

	return 0;
}

void *worker_start_routine(void *arg)
{
	return NULL;
}

nosv_worker_t *worker_create(cpu_t *cpu)
{
	int ret;

	nosv_worker_t *worker = (nosv_worker_t *) salloc(sizeof(nosv_worker_t), /* TODO */ 0);

	pthread_attr_t attr;
	ret = pthread_attr_init(&attr);
	if (ret)
		nosv_abort("Cannot create pthread attributes");

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
