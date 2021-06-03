/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <unistd.h>

#include "common.h"
#include "hardware/cpus.h"
#include "hardware/threads.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"

thread_local nosv_worker_t *current_worker;
thread_manager_t *current_process_manager;
atomic_int threads_shutdown_signal;

void threadmanager_init(thread_manager_t *threadmanager)
{
	atomic_init(&threads_shutdown_signal, 0);
	nosv_spin_init(&threadmanager->idle_spinlock);
	nosv_spin_init(&threadmanager->shutdown_spinlock);
	list_init(&threadmanager->idle_threads);
	clist_init(&threadmanager->shutdown_threads);

	current_process_manager = threadmanager;
}

void threadmanager_shutdown(thread_manager_t *threadmanager)
{
	assert(threadmanager);

	// Threads should shutdown at this moment
	atomic_store_explicit(&threads_shutdown_signal, 1, memory_order_relaxed);

	int join = 0;
	while (!join) {
		nosv_spin_lock(&threadmanager->idle_spinlock);
		list_head_t *head = list_pop_head(&threadmanager->idle_threads);
		while (head) {
			nosv_worker_t *worker = list_elem(head, nosv_worker_t, list_hook);
			worker_wakeup(worker, NULL);

			head = list_pop_head(&threadmanager->idle_threads);
		}
		nosv_spin_unlock(&threadmanager->idle_spinlock);

		nosv_spin_lock(&threadmanager->shutdown_spinlock);
		int destroyed = clist_count(&threadmanager->shutdown_threads);
		int threads = atomic_load_explicit(&threadmanager->created, memory_order_acquire);

		join = (threads == destroyed);
		assert(threads >= destroyed);
		nosv_spin_unlock(&threadmanager->shutdown_spinlock);

		// Sleep for a ms
		if (!join)
			usleep(1000);
	}

	// We don't really need locking anymore
	list_head_t *head = clist_pop_head(&threadmanager->shutdown_threads);

	while (head) {
		nosv_worker_t *worker = list_elem(head, nosv_worker_t, list_hook);
		worker_join(worker);
		sfree(worker, sizeof(nosv_worker_t), cpu_get_current());
		head = clist_pop_head(&threadmanager->shutdown_threads);
	}
}

void *worker_start_routine(void *arg)
{
	current_worker = (nosv_worker_t *)arg;
	cpu_set_current(current_worker->cpu->logic_id);

	while (!atomic_load_explicit(&threads_shutdown_signal, memory_order_relaxed)) {
		usleep(1000); // This should be the body?
	}

	nosv_spin_lock(&current_process_manager->shutdown_spinlock);
	clist_add(&current_process_manager->shutdown_threads, &current_worker->list_hook);
	nosv_spin_unlock(&current_process_manager->shutdown_spinlock);

	return NULL;
}

int worker_should_shutdown()
{
	return atomic_load_explicit(&threads_shutdown_signal, memory_order_relaxed);
}

void worker_wakeup(nosv_worker_t *worker, cpu_t *cpu)
{
	// NOP for now
}

nosv_worker_t *worker_create(thread_manager_t *threadmanager, cpu_t *cpu)
{
	int ret;
	atomic_fetch_add_explicit(&threadmanager->created, 1, memory_order_release);

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
