/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>
#include <stdatomic.h>

#include "compiler.h"
#include "defaults.h"
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

	// TID of the thread that created the delegate
	// (only used for instrumentation)
	pid_t delegate_creator_tid;
} thread_manager_t;

typedef struct nosv_worker {
	// Hook for linked lists
	list_head_t list_hook;
	// Kernel-Level thread this worker represents
	pthread_t kthread;
	// Current CPU
	cpu_t *cpu;
	// New CPU, used when the worker is re-submitted
	cpu_t *new_cpu;
	// Task the worker is currently executing
	nosv_task_t task;
	// Immediate Successor task
	nosv_task_t immediate_successor;
	// Condition variable to block the worker
	nosv_condvar_t condvar;
	// Linux Thread ID
	pid_t tid;
	// Linux Thread ID of the creator thread (-1 if unknown)
	pid_t creator_tid;
	// Logic process ID of this thread
	int logic_pid;
	// Original CPU Set (used for external workers)
	cpu_set_t original_affinity;
} nosv_worker_t;

__internal void threadmanager_init(thread_manager_t *threadmanager);
__internal void threadmanager_shutdown(thread_manager_t *threadmanager);

__internal void worker_yield(void);
__internal int worker_yield_if_needed(nosv_task_t current_task);
__internal void worker_block(void);
__internal void worker_add_to_idle_list(void);
__internal nosv_worker_t *worker_create_local(thread_manager_t *threadmanager, cpu_t *cpu, nosv_task_t task);
__internal nosv_worker_t *worker_create_external(void);
__internal void worker_free_external(nosv_worker_t *worker);
__internal void worker_wake_idle(int pid, cpu_t *cpu, nosv_task_t task);
__internal void worker_join(nosv_worker_t *worker);
__internal int worker_is_in_task(void);
__internal nosv_worker_t *worker_current(void);
__internal nosv_task_t worker_current_task(void);
__internal nosv_task_t worker_get_immediate(void);
__internal void worker_set_immediate(nosv_task_t task);

__internal int worker_should_shutdown(void);

#endif // THREADS_H
