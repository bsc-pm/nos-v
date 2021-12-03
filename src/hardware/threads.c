/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "common.h"
#include "compat.h"
#include "compiler.h"
#include "generic/arch.h"
#include "hardware/cpus.h"
#include "hardware/pids.h"
#include "hardware/threads.h"
#include "hwcounters/hwcounters.h"
#include "instr.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"
#include "scheduler/scheduler.h"
#include "system/tasks.h"

__internal thread_local nosv_worker_t *current_worker = NULL;
__internal thread_manager_t *current_process_manager = NULL;
__internal atomic_int threads_shutdown_signal;

// The delegate thread is used to create remote workers
static inline void *delegate_routine(void *args)
{
	thread_manager_t *threadmanager = (thread_manager_t *)args;

	instr_thread_init();
	instr_thread_execute(-1, threadmanager->delegate_creator_tid, (uint64_t) args);
	instr_delegate_enter();

	event_queue_t *queue = &threadmanager->thread_creation_queue;
	creation_event_t event;

	while (1) {
		instr_thread_pause();
		event_queue_pull(queue, &event);
		instr_thread_resume();

		if (event.type == Shutdown)
			break;

		assert(event.type == Creation);
		worker_create_local(threadmanager, event.cpu, event.task);
	}

	instr_delegate_exit();
	instr_thread_end();

	return NULL;
}

static inline void delegate_thread_create(thread_manager_t *threadmanager)
{
	// TODO Standalone should have affinity here?
	// We use the address of the threadmanager structure as it
	// provides a unique tag known to this thread and the delegate.
	instr_thread_create(-1, (uint64_t) threadmanager);
	int ret = pthread_create(&threadmanager->delegate_thread, NULL, delegate_routine, threadmanager);
	if (ret)
		nosv_abort("Cannot create pthread");
}

static inline void worker_wake_internal(nosv_worker_t *worker, cpu_t *cpu)
{
	assert(worker);
	assert(worker->tid != 0);
	assert(worker != current_worker);

	worker->new_cpu = cpu;

	// CPU may be NULL
	if (cpu) {
		// if we are waking up a thread of a different process, keep
		// track of the new running process in the cpumanager
		if (worker->logic_pid != logic_pid)
			cpu_set_pid(cpu, worker->logic_pid);

		// if the worker was not previously bound to the core where it
		// is going to wake up now, bind it remotely now
		if (worker->cpu != cpu) {
			instr_affinity_remote(cpu->logic_id, worker->tid);
			if (unlikely(sched_setaffinity(worker->tid, sizeof(cpu->cpuset), &cpu->cpuset)))
				nosv_abort("Cannot change thread affinity");
		}
	} else {
		// We're waking up a thread without a CPU, which may happen on nOS-V shutdown
		// Reset its affinity to the original CPU mask
		instr_affinity_remote(-1, worker->tid);
		if (unlikely(sched_setaffinity(worker->tid, sizeof(cpumanager->all_cpu_set), &cpumanager->all_cpu_set)))
			nosv_abort("Cannot change thread affinity");
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
	threadmanager->delegate_creator_tid = gettid();

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
		instr_thread_cool();
		if (!is_busy_worker)
			worker_add_to_idle_list();

		worker_wake_internal(task->worker, cpu);
		worker_block();
	} else if (task->type->pid != logic_pid) {
		// The task has not started yet but it is from a PID other than the
		// current one. Then, wake up an idle thread from the task's PID
		instr_thread_cool();
		if (!is_busy_worker)
			worker_add_to_idle_list();

		cpu_transfer(task->type->pid, cpu, task);
		worker_block();
	} else if (is_busy_worker) {
		// The task has not started and it is from the current PID, but the
		// current worker is busy and cannot execute directly the task. Then
		// delegate the work and wake up an idle thread from this PID to
		// execute the task
		instr_thread_cool();
		worker_wake_idle(logic_pid, cpu, task);
		worker_block();
	} else {
		// Otherwise, start running the task because the current thread is
		// valid to run the task and it is idle
		task->worker = current_worker;
		current_worker->task = task;

		task_execute(task);

		current_worker->task = NULL;

		// The task execution has ended, so do not block the thread
	}
}

static inline void *worker_start_routine(void *arg)
{
	current_worker = (nosv_worker_t *)arg;
	assert(current_worker);
	assert(current_worker->cpu);
	cpu_set_current(current_worker->cpu->logic_id);
	current_worker->tid = gettid();

	// Initialize hardware counters for the thread
	hwcounters_thread_initialize(current_worker);

	// Set turbo settings if enabled
	if (nosv_config.turbo_enabled)
		arch_enable_turbo();

	instr_thread_init();
	instr_thread_execute(current_worker->cpu->logic_id, current_worker->creator_tid, (uint64_t) arg);
	instr_worker_enter();

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
	// Before transfering it, update runtime counters
	if (current_worker->cpu) {
		hwcounters_update_runtime_counters();
		pidmanager_transfer_to_idle(current_worker->cpu);
	}

	nosv_spin_lock(&current_process_manager->shutdown_spinlock);
	clist_add(&current_process_manager->shutdown_threads, &current_worker->list_hook);
	nosv_spin_unlock(&current_process_manager->shutdown_spinlock);

	hwcounters_thread_shutdown(current_worker);

	instr_worker_exit();
	instr_thread_end();

	return NULL;
}

void worker_add_to_idle_list(void)
{
	nosv_spin_lock(&current_process_manager->idle_spinlock);
	list_add(&current_process_manager->idle_threads, &current_worker->list_hook);
	nosv_spin_unlock(&current_process_manager->idle_spinlock);

	// Before a thread blocks (idles), update runtime counters
	hwcounters_update_runtime_counters();
}

int worker_should_shutdown(void)
{
	return atomic_load_explicit(&threads_shutdown_signal, memory_order_relaxed);
}

void worker_yield(void)
{
	assert(current_worker);

	// Inform the instrumentation that this thread is going to be paused
	// *before* we wake another thread. The thread is put in the cooling
	// state, to prevent two threads in the running state in the same CPU.
	instr_thread_cool();

	// Block this thread and place another one. This is called on nosv_pause
	// First, wake up another worker in this cpu, one from the same PID
	worker_wake_idle(logic_pid, current_worker->cpu, NULL);

	// Then, sleep and return once we have been woken up
	worker_block();
}

int worker_yield_if_needed(nosv_task_t current_task)
{
	assert(current_worker);
	assert(current_worker->task == current_task);

	cpu_t *cpu = current_worker->cpu;
	assert(cpu);

	instr_sched_hungry();

	// Try to get a ready task without blocking
	nosv_task_t new_task = scheduler_get(cpu->logic_id, SCHED_GET_NONBLOCKING);

	instr_sched_fill();

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

	// Before blocking the thread's execution, update runtime counters
	hwcounters_update_runtime_counters();

	instr_thread_pause();

	// Blocking operation
	nosv_condvar_wait(&current_worker->condvar);

	// We are back. At this point we have already been migrated to the right
	// core by the worker that has woken us up.

	// Update CPU in case of migration
	// We use a different variable to detect cpu changes and prevent races
	cpu_t *oldcpu = current_worker->cpu;
	current_worker->cpu = current_worker->new_cpu;
	cpu_t *cpu = current_worker->cpu;

	if (!cpu) {
		cpu_set_current(-1);
	} else if (cpu != oldcpu) {
		cpu_set_current(cpu->logic_id);
	}

	instr_thread_resume();
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
	if (pid == logic_pid) {
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
	worker->logic_pid = logic_pid;
	worker->immediate_successor = NULL;
	worker->creator_tid = gettid();
	worker->in_task_body = 0;
	nosv_condvar_init(&worker->condvar);

	pthread_attr_t attr;
	ret = pthread_attr_init(&attr);
	if (ret)
		nosv_abort("Cannot create pthread attributes");

	pthread_attr_setaffinity_np(&attr, sizeof(cpu->cpuset), &cpu->cpuset);

	// We use the address of the worker structure as the tag of the
	// thread create event, as it provides a unique value known to
	// both threads.
	instr_thread_create(cpu->logic_id, (uint64_t) worker);

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
	worker->logic_pid = logic_pid;
	nosv_condvar_init(&worker->condvar);
	current_worker = worker;
	worker->immediate_successor = NULL;
	worker->creator_tid = -1;
	worker->in_task_body = 1;
	sched_getaffinity(0, sizeof(worker->original_affinity), &worker->original_affinity);

	// Initialize hardware counters for the thread
	hwcounters_thread_initialize(worker);

	// Set turbo settings if enabled
	if (nosv_config.turbo_enabled)
		arch_enable_turbo();

	return worker;
}

void worker_free_external(nosv_worker_t *worker)
{
	assert(worker);

	// Initialize hardware counters for the thread
	hwcounters_thread_shutdown(worker);

	nosv_condvar_destroy(&worker->condvar);
	sfree(worker, sizeof(nosv_worker_t), cpu_get_current());
	assert(worker == current_worker);

	current_worker = NULL;
}

void worker_join(nosv_worker_t *worker)
{
	// NOTE: No need to shutdown hwcounters here, as it is done in worker_start_routine

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
