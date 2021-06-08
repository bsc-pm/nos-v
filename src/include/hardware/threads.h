/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>
#include <stdatomic.h>

#include "compiler.h"
#include "climits.h"
#include "nosv.h"
#include "generic/bitset.h"
#include "generic/list.h"
#include "generic/mutex.h"
#include "generic/spinlock.h"
#include "generic/condvar.h"
#include "hardware/cpus.h"
#include "hardware/eventqueue.h"

extern atomic_int threads_shutdown_signal;

typedef struct thread_manager {
	nosv_spinlock_t idle_spinlock;
	list_head_t idle_threads;

	nosv_spinlock_t shutdown_spinlock;
	clist_head_t shutdown_threads;

	atomic_int created;
	event_queue_t thread_creation_queue;
	pthread_t delegate_thread;
} thread_manager_t;

typedef struct nosv_worker {
	list_head_t list_hook;
	pthread_t kthread;
	cpu_t *cpu;
	nosv_task_t task;
	nosv_condvar_t condvar;
} nosv_worker_t;

__internal void threadmanager_init(thread_manager_t *threadmanager);
__internal void threadmanager_shutdown(thread_manager_t *threadmanager);

__internal void worker_yield();
__internal void worker_block();
__internal void worker_idle();
__internal nosv_worker_t *worker_create_local(thread_manager_t *threadmanager, cpu_t *cpu, nosv_task_t task);
__internal nosv_worker_t *worker_create_external();
__internal void worker_free_external(nosv_worker_t *worker);
__internal void worker_wake(int pid, cpu_t *cpu, nosv_task_t task);
__internal void worker_join(nosv_worker_t *worker);
__internal int worker_is_in_task();
__internal nosv_worker_t *worker_current();
__internal nosv_task_t worker_current_task();

__internal int worker_should_shutdown();

#endif // THREADS_H