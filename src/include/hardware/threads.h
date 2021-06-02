/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>

#include "compiler.h"
#include "climits.h"
#include "generic/bitset.h"
#include "generic/list.h"
#include "generic/mutex.h"
#include "generic/spinlock.h"
#include "hardware/cpus.h"

typedef struct thread_manager {
	nosv_spinlock_t idle_spinlock;
	list_head_t idle_threads;

	nosv_spinlock_t shutdown_spinlock;
	list_head_t shutdown_threads;
} thread_manager_t;

typedef struct nosv_worker {
	list_head_t list_hook;
	pthread_t kthread;
	cpu_t *cpu;
} nosv_worker_t;

__internal void threadmanager_init(thread_manager_t *threadmanager);
__internal nosv_worker_t *worker_create(cpu_t *cpu);
__internal void worker_join(nosv_worker_t *worker);

#endif // THREADS_H