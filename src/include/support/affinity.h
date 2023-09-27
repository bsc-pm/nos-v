/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_AFFINITY_SUPPORT_H
#define NOSV_AFFINITY_SUPPORT_H

#include <pthread.h>
#include <sched.h>

#include "compiler.h"
#include "hardware/threads.h"


__internal void affinity_support_init(void);
__internal void affinity_support_register_worker(nosv_worker_t *worker);
__internal void affinity_support_unregister_worker(nosv_worker_t *worker, char restore);

__internal int bypass_pthread_create(
	pthread_t *restrict thread,
	const pthread_attr_t *restrict attr,
	void *(*start_routine)(void *),
	void *restrict arg
);
__internal int bypass_sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask);
__internal int bypass_sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask);
__internal int bypass_pthread_setaffinity_np(pthread_t thread,size_t cpusetsize,const cpu_set_t *cpuset);
__internal int bypass_pthread_getaffinity_np(pthread_t thread,size_t cpusetsize,cpu_set_t *cpuset);

#endif // NOSV_AFFINITY_SUPPORT_H
