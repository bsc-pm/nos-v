/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <unistd.h>

#include "common.h"
#include "hardware/cpus.h"
#include "hardware/threads.h"
#include "hardware/pids.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"
#include "scheduler/scheduler.h"
#include "system/tasks.h"

thread_local nosv_worker_t *current_worker = NULL;
thread_manager_t *current_process_manager = NULL;
atomic_int threads_shutdown_signal;

// The delegate thread is used to create remote workers
void *delegate_routine(void *args)
{
	thread_manager_t *threadmanager = (thread_manager_t *)args;
	event_queue_t *queue = &threadmanager->thread_creation_queue;
	creation_event_t event;

	while (1) {
		event_queue_pull(queue, &event);

		if (event.type == Shutdown)
			break;

		assert(event.type == Creation);
		worker_create_local(threadmanager, event.cpu, event.task);
	}

	return NULL;
}

static inline void delegate_thread_create(thread_manager_t *threadmanager)
{
	// TODO Standalone should have affinity here?
	int ret = pthread_create(&threadmanager->delegate_thread, NULL, delegate_routine, threadmanager);
	if (ret)
		nosv_abort("Cannot create pthread");
}

static inline void worker_wakeup_internal(nosv_worker_t *worker, cpu_t *cpu)
{
	worker->cpu = cpu;
	// Remotely set thread affinity before waking up
	pthread_setaffinity_np(worker->kthread, sizeof(cpu->cpuset), &cpu->cpuset);
	// Now wake up the thread
	nosv_condvar_signal(&worker->condvar);
}

void threadmanager_init(thread_manager_t *threadmanager)
{
	atomic_init(&threads_shutdown_signal, 0);
	nosv_spin_init(&threadmanager->idle_spinlock);
	nosv_spin_init(&threadmanager->shutdown_spinlock);
	list_init(&threadmanager->idle_threads);
	clist_init(&threadmanager->shutdown_threads);
	event_queue_init(&threadmanager->thread_creation_queue);

	current_process_manager = threadmanager;

	delegate_thread_create(threadmanager);
}

void threadmanager_shutdown(thread_manager_t *threadmanager)
{
	assert(threadmanager);

	// Threads should shutdown at this moment
	atomic_store_explicit(&threads_shutdown_signal, 1, memory_order_relaxed);

	// Ask the delegate thread to shutdown as well
	creation_event_t event;
	event.type = Shutdown;
	event_queue_put(&threadmanager->thread_creation_queue, &event);

	int join = 0;
	while (!join) {
		nosv_spin_lock(&threadmanager->idle_spinlock);
		list_head_t *head = list_pop_head(&threadmanager->idle_threads);
		while (head) {
			nosv_worker_t *worker = list_elem(head, nosv_worker_t, list_hook);
			worker_wakeup_internal(worker, NULL);

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

	// Join delegate as well
	pthread_join(threadmanager->delegate_thread, NULL);
}

void *worker_start_routine(void *arg)
{
	current_worker = (nosv_worker_t *)arg;
	cpu_set_current(current_worker->cpu->logic_id);

	while (!atomic_load_explicit(&threads_shutdown_signal, memory_order_relaxed)) {
		nosv_task_t task = current_worker->task;

		if (!task)
			task = scheduler_get(cpu_get_current());

		if (task) {
			int task_pid = task->type->pid;

			if (task->worker != NULL) {
				worker_wakeup_internal(task->worker, current_worker->cpu);
				worker_block();
			} else if (task_pid != logical_pid) {
				cpu_transfer(task_pid, current_worker->cpu, task);
			} else {
				task->worker = current_worker;
				current_worker->task = task;

				task_execute(task);

				current_worker->task = NULL;
			}
		}
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

void worker_yield()
{
	assert(current_worker);
	// Block this thread and place another one. This is called on nosv_pause
	// First, wake up another worker in this cpu, one from the same PID
	worker_wake(logical_pid, current_worker->cpu, NULL);

	// Then, sleep and return once we have been woken up
	worker_block();
}

// Returns new CPU
void worker_block()
{
	assert(current_worker);
	// Blocking operation
	nosv_condvar_wait(&current_worker->condvar);
	// We are back. Update CPU in case of migration
	cpu_set_current(current_worker->cpu->logic_id);
}

static inline void worker_create_remote(thread_manager_t *threadmanager, cpu_t *cpu, nosv_task_t task)
{
	creation_event_t event;
	event.cpu = cpu;
	event.task = task;
	event.type = Creation;

	event_queue_put(&threadmanager->thread_creation_queue, &event);
}

void worker_wake(int pid, cpu_t *cpu, nosv_task_t task)
{
	// Find the remote thread manager
	thread_manager_t *threadmanager = pidmanager_get_threadmanager(pid);
	assert(threadmanager);

	nosv_spin_lock(&threadmanager->idle_spinlock);
	list_head_t *head = list_pop_head(&threadmanager->idle_threads);
	nosv_spin_unlock(&threadmanager->idle_spinlock);

	if (head) {
		// We have an idle thread to wake up
		nosv_worker_t *worker = list_elem(head, nosv_worker_t, list_hook);
		assert(!worker->task);
		worker->task = task;
		worker_wakeup_internal(worker, cpu);
		return;
	}

	// We need to create a new worker.
	if (pid == logical_pid) {
		worker_create_local(threadmanager, cpu, task);
	} else {
		worker_create_remote(threadmanager, cpu, task);
	}
}

nosv_worker_t *worker_create_local(thread_manager_t *threadmanager, cpu_t *cpu, nosv_task_t task)
{
	int ret;
	atomic_fetch_add_explicit(&threadmanager->created, 1, memory_order_release);

	nosv_worker_t *worker = (nosv_worker_t *)salloc(sizeof(nosv_worker_t), cpu_get_current());
	worker->cpu = cpu;

	pthread_attr_t attr;
	ret = pthread_attr_init(&attr);
	if (ret)
		nosv_abort("Cannot create pthread attributes");

	pthread_attr_setaffinity_np(&attr, sizeof(cpu->cpuset), &cpu->cpuset);

	ret = pthread_create(&worker->kthread, &attr, worker_start_routine, worker);
	if (ret)
		nosv_abort("Cannot create pthread");

	nosv_condvar_init(&worker->condvar);

	return worker;
}

void worker_join(nosv_worker_t *worker)
{
	int ret = pthread_join(worker->kthread, NULL);
	if (ret)
		nosv_abort("Cannot join pthread");
}

int worker_is_in_task() {
	if (!current_worker)
		return 0;

	if (!current_worker->task)
		return 0;

	return 1;
}