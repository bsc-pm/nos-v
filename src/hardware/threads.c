/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "common.h"
#include "compiler.h"
#include "hardware/cpus.h"
#include "hardware/threads.h"
#include "hardware/pids.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"
#include "scheduler/scheduler.h"
#include "system/tasks.h"
#include "instr.h"

#ifndef gettid
# define gettid() ((pid_t)syscall(SYS_gettid))
#endif

__internal thread_local nosv_worker_t *current_worker = NULL;
__internal thread_manager_t *current_process_manager = NULL;
__internal atomic_int threads_shutdown_signal;

// The delegate thread is used to create remote workers
static inline void *delegate_routine(void *args)
{

	thread_manager_t *threadmanager = (thread_manager_t *)args;

	instr_thread_init();
	instr_thread_execute(-1, threadmanager->creator_tid, args);

	event_queue_t *queue = &threadmanager->thread_creation_queue;
	creation_event_t event;

	while (1) {
		event_queue_pull(queue, &event);

		if (event.type == Shutdown)
			break;

		assert(event.type == Creation);
		worker_create_local(threadmanager, event.cpu, event.task);
	}

	instr_thread_end();

	return NULL;
}

static inline void delegate_thread_create(thread_manager_t *threadmanager)
{
	// TODO Standalone should have affinity here?
	instr_thread_create(-1, threadmanager);
	int ret = pthread_create(&threadmanager->delegate_thread, NULL, delegate_routine, threadmanager);
	if (ret)
		nosv_abort("Cannot create pthread");
}

static inline void worker_wake_internal(nosv_worker_t *worker, cpu_t *cpu)
{
	// CPU may be NULL
	worker->new_cpu = cpu;

	if (cpu && worker->pid == logical_pid) {
		// Remotely set thread affinity before waking up, to prevent disturbing another CPU
		instr_affinity_remote(cpu->logic_id, worker->tid);
		pthread_setaffinity_np(worker->kthread, sizeof(cpu->cpuset), &cpu->cpuset);
	} else if (cpu && worker->pid != logical_pid) {
		cpu_set_pid(cpu, worker->pid);
	}
	// Now wake up the thread
	nosv_condvar_signal(&worker->condvar);
}

void threadmanager_init(thread_manager_t *threadmanager)
{
	atomic_init(&threads_shutdown_signal, 0);
	atomic_init(&threadmanager->created, 0);
	list_init(&threadmanager->idle_threads);
	clist_init(&threadmanager->shutdown_threads);
	nosv_spin_init(&threadmanager->idle_spinlock);
	nosv_spin_init(&threadmanager->shutdown_spinlock);
	event_queue_init(&threadmanager->thread_creation_queue);
	threadmanager->creator_tid = gettid();

	current_process_manager = threadmanager;

	delegate_thread_create(threadmanager);
}

void threadmanager_shutdown(thread_manager_t *threadmanager)
{
	assert(threadmanager);
	// This should happen outside of a worker
	assert(worker_current() == NULL);

	// Threads should shutdown at this moment
	atomic_store_explicit(&threads_shutdown_signal, 1, memory_order_relaxed);

	// Ask the delegate thread to shutdown as well
	creation_event_t event;
	event.type = Shutdown;
	event_queue_put(&threadmanager->thread_creation_queue, &event);

	// Join the delegate *first*, to prevent any races
	pthread_join(threadmanager->delegate_thread, NULL);

	int join = 0;
	while (!join) {
		nosv_spin_lock(&threadmanager->idle_spinlock);
		list_head_t *head = list_pop_head(&threadmanager->idle_threads);
		while (head) {
			nosv_worker_t *worker = list_elem(head, nosv_worker_t, list_hook);
			worker_wake_internal(worker, NULL);

			head = list_pop_head(&threadmanager->idle_threads);
		}
		nosv_spin_unlock(&threadmanager->idle_spinlock);

		nosv_spin_lock(&threadmanager->shutdown_spinlock);
		size_t destroyed = clist_count(&threadmanager->shutdown_threads);
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
		nosv_condvar_destroy(&worker->condvar);
		sfree(worker, sizeof(nosv_worker_t), -1);
		head = clist_pop_head(&threadmanager->shutdown_threads);
	}
}

static inline void worker_execute_or_delegate(nosv_task_t task, cpu_t *cpu, int is_busy_worker)
{
	assert(task);
	assert(cpu);

	if (task->worker != NULL) {
		// Another thread was already running the task, so we have to resume
		// the execution of the thread
		worker_wake_internal(task->worker, cpu);
	} else if (task->type->pid != logical_pid) {
		// The task has not started yet but it is from a PID other than the
		// current one. Then, wake up an idle thread from the the task's PID
		cpu_transfer(task->type->pid, cpu, task);
	} else if (is_busy_worker) {
		// The task has not started and it is from the current PID, but the
		// current worker is busy and cannot execute directly the task. Then
		// delegate the work and wake up an idle thread from this PID to
		// execute the task
		worker_wake_idle(logical_pid, cpu, task);
	} else {
		// Otherwise, start running the task because the current thread is
		// valid to run the task and it is idle
		task->worker = current_worker;
		current_worker->task = task;

		task_execute(task);

		current_worker->task = NULL;

		// The task execution has ended, so do not block the thread
		return;
	}

	// Block the worker if the task was delegated to another thread
	if (is_busy_worker)
		// Only block the current worker
		worker_block();
	else
		// Block the current worker but also mark it as idle
		worker_idle();
}

static inline void *worker_start_routine(void *arg)
{
	current_worker = (nosv_worker_t *)arg;
	assert(current_worker);
	assert(current_worker->cpu);
	cpu_set_current(current_worker->cpu->logic_id);
	current_worker->tid = gettid();

	instr_thread_init();
	instr_thread_execute(current_worker->cpu->logic_id, current_worker->creator_tid, arg);

	// At the initialization, we signal the instrumentation to state
	// that we are looking for work.
	instr_sched_hungry();

	while (!atomic_load_explicit(&threads_shutdown_signal, memory_order_relaxed)) {
		nosv_task_t task = current_worker->task;

		if (!task && current_worker->immediate_successor) {
			task = current_worker->immediate_successor;
			worker_set_immediate(NULL);
		}

		if (!task && current_worker->cpu)
			task = scheduler_get(cpu_get_current(), SCHED_GET_DEFAULT);

		if (task) {
			// We can only reach this point in two cases:
			// 1) When this thread requests a task as
			// client, and is assigned one by another
			// scheduler server thread.
			// 2) When this thread is the scheduler server
			// and exits the scheduler serving loop because
			// it has a task assigned.
			//
			// Therefore, we are now filled with work.
			instr_sched_fill();

			worker_execute_or_delegate(task, current_worker->cpu, /* idle thread */ 0);

			// As soon as the task is handled, we are now
			// looking for more work, so we call here
			// instr_sched_hungry only once, so avoid filling
			// the tracing buffer with sched enter events.
			instr_sched_hungry();
		}
	}

	// After the main loop we are no longer hungry (for tasks)
	instr_sched_fill();

	assert(!worker_get_immediate());

	// Before shutting down, we have to transfer our active CPU if we still have one
	// We don't have one if we were woken up from the idle thread pool direcly
	if (current_worker->cpu)
		pidmanager_transfer_to_idle(current_worker->cpu);

	nosv_spin_lock(&current_process_manager->shutdown_spinlock);
	clist_add(&current_process_manager->shutdown_threads, &current_worker->list_hook);
	nosv_spin_unlock(&current_process_manager->shutdown_spinlock);

	instr_thread_end();

	return NULL;
}

void worker_idle(void)
{
	nosv_spin_lock(&current_process_manager->idle_spinlock);
	list_add(&current_process_manager->idle_threads, &current_worker->list_hook);
	nosv_spin_unlock(&current_process_manager->idle_spinlock);
	worker_block();
}

int worker_should_shutdown(void)
{
	return atomic_load_explicit(&threads_shutdown_signal, memory_order_relaxed);
}

void worker_yield(void)
{
	assert(current_worker);

	// Block this thread and place another one. This is called on nosv_pause
	// First, wake up another worker in this cpu, one from the same PID
	worker_wake_idle(logical_pid, current_worker->cpu, NULL);

	// Then, sleep and return once we have been woken up
	worker_block();
}

int worker_yield_if_needed(nosv_task_t current_task)
{
	assert(current_worker);
	assert(current_worker->task == current_task);

	cpu_t *cpu = current_worker->cpu;
	assert(cpu);

	// Try to get a ready task without blocking
	nosv_task_t new_task = scheduler_get(cpu->logic_id, SCHED_GET_NONBLOCKING);
	if (!new_task)
		return 0;

	// We retrieved a ready task, so submit the current one
	scheduler_submit(current_task);

	// Wake up the corresponding thread to execute the task
	worker_execute_or_delegate(new_task, cpu, /* busy thread */ 1);

	return 1;
}

// Returns new CPU
void worker_block(void)
{
	assert(current_worker);

	instr_thread_pause();

	// Blocking operation
	nosv_condvar_wait(&current_worker->condvar);

	instr_thread_resume();

	// We are back. Update CPU in case of migration
	// We use a different variable to detect cpu changes and prevent races
	cpu_t *oldcpu = current_worker->cpu;
	current_worker->cpu = current_worker->new_cpu;
	cpu_t *cpu = current_worker->cpu;

	if(!cpu) {
		instr_affinity_set(-1);
		cpu_set_current(-1);
	} else if (cpu != oldcpu) {
		cpu_set_current(cpu->logic_id);
		sched_setaffinity(current_worker->tid, sizeof(cpu->cpuset), &cpu->cpuset);
		instr_affinity_set(cpu->logic_id);
	}
}

static inline void worker_create_remote(thread_manager_t *threadmanager, cpu_t *cpu, nosv_task_t task)
{
	creation_event_t event;
	event.cpu = cpu;
	event.task = task;
	event.type = Creation;

	event_queue_put(&threadmanager->thread_creation_queue, &event);
}

void worker_wake_idle(int pid, cpu_t *cpu, nosv_task_t task)
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
		worker_wake_internal(worker, cpu);
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
	assert(cpu);

	nosv_worker_t *worker = (nosv_worker_t *)salloc(sizeof(nosv_worker_t), cpu_get_current());
	worker->cpu = cpu;
	worker->task = task;
	worker->pid = logical_pid;
	worker->immediate_successor = NULL;
	worker->creator_tid = gettid();
	nosv_condvar_init(&worker->condvar);

	pthread_attr_t attr;
	ret = pthread_attr_init(&attr);
	if (ret)
		nosv_abort("Cannot create pthread attributes");

	pthread_attr_setaffinity_np(&attr, sizeof(cpu->cpuset), &cpu->cpuset);

	instr_thread_create(cpu->logic_id, worker);

	ret = pthread_create(&worker->kthread, &attr, worker_start_routine, worker);
	if (ret)
		nosv_abort("Cannot create pthread");

	pthread_attr_destroy(&attr);

	return worker;
}

nosv_worker_t *worker_create_external(void)
{
	nosv_worker_t *worker = (nosv_worker_t *)salloc(sizeof(nosv_worker_t), cpu_get_current());
	worker->cpu = NULL;
	worker->task = NULL;
	worker->kthread = pthread_self();
	worker->tid = gettid();
	worker->pid = logical_pid;
	nosv_condvar_init(&worker->condvar);
	current_worker = worker;
	worker->immediate_successor = NULL;
	sched_getaffinity(0, sizeof(worker->original_affinity), &worker->original_affinity);

	/* The thread may be already initialized */
	instr_thread_init();

	return worker;
}

void worker_free_external(nosv_worker_t *worker)
{
	assert(worker);
	nosv_condvar_destroy(&worker->condvar);
	sfree(worker, sizeof(nosv_worker_t), cpu_get_current());
	assert(worker == current_worker);
	current_worker = NULL;
}

void worker_join(nosv_worker_t *worker)
{
	int ret = pthread_join(worker->kthread, NULL);
	if (ret)
		nosv_abort("Cannot join pthread");
}

int worker_is_in_task(void)
{
	if (!current_worker)
		return 0;

	if (!current_worker->task)
		return 0;

	return 1;
}

nosv_worker_t *worker_current(void)
{
	return current_worker;
}

nosv_task_t worker_current_task(void)
{
	if (!current_worker)
		return NULL;

	return current_worker->task;
}

nosv_task_t worker_get_immediate(void)
{
	if (!current_worker)
		return NULL;

	return current_worker->immediate_successor;
}

void worker_set_immediate(nosv_task_t task)
{
	current_worker->immediate_successor = task;
}
