/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "common.h"
#include "compat.h"
#include "compiler.h"
#include "generic/arch.h"
#include "generic/list.h"
#include "hardware/cpus.h"
#include "hardware/pids.h"
#include "hardware/threads.h"
#include "hwcounters/hwcounters.h"
#include "instr.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"
#include "scheduler/scheduler.h"
#include "support/affinity.h"
#include "system/tasks.h"

__internal thread_local nosv_worker_t *current_worker = NULL;
__internal thread_manager_t *current_process_manager = NULL;
__internal atomic_int threads_shutdown_signal;
__internal thread_local struct kinstr *kinstr = NULL;

// The delegate thread is used to create remote workers
static inline void *delegate_routine(void *args)
{
	thread_manager_t *threadmanager = (thread_manager_t *)args;

	instr_thread_init();
	instr_thread_execute(-1, threadmanager->delegate_creator_tid, (uint64_t) args);
	instr_delegate_enter();
	instr_kernel_init(&kinstr);

	event_queue_t *queue = &threadmanager->thread_creation_queue;
	creation_event_t event;

	while (1) {
		instr_thread_pause();
		event_queue_pull(queue, &event);
		instr_thread_resume();
		instr_kernel_flush(kinstr);

		if (event.type == Shutdown)
			break;

		assert(event.type == Creation);
		worker_create_local(threadmanager, event.cpu, event.handle);
	}

	instr_delegate_exit();
	instr_kernel_flush(kinstr);
	instr_thread_end();

	return NULL;
}

static inline void common_pthread_create(
	pthread_t *thread,
	void *(*start_routine)(void *),
	void *arg,
	cpu_set_t *cpuset
) {
	pthread_attr_t attr;
	int ret = pthread_attr_init(&attr);
	if (unlikely(ret))
		nosv_abort("Could not initialize pthread attributes");

	ret = pthread_attr_setstacksize(&attr, nosv_config.thread_stack_size);
	if (unlikely(ret))
		nosv_warn("Could not set thread stack size. Is misc.stack_size a multiple of the OS page size?");

	if (cpuset) {
		ret = pthread_attr_setaffinity_np(&attr, sizeof(*cpuset), cpuset);
		// Non-critical, but bad
		if (unlikely(ret))
			nosv_abort("Could not set thread affinity correctly during creation");
	}

	ret = bypass_pthread_create(thread, &attr, start_routine, arg);
	if (unlikely(ret))
		nosv_abort("Cannot create pthread");

	ret = pthread_attr_destroy(&attr);
	if (unlikely(ret))
		nosv_warn("Could not destroy pthread attributes");
}

static inline void delegate_thread_create(thread_manager_t *threadmanager)
{
	// TODO: Standalone should have affinity here?
	// We use the address of the threadmanager structure as it
	// provides a unique tag known to this thread and the delegate.
	instr_thread_create(-1, (uint64_t) threadmanager);

	common_pthread_create(&threadmanager->delegate_thread, delegate_routine, threadmanager, NULL);
}

static inline void worker_wake_internal(nosv_worker_t *worker, cpu_t *cpu)
{
	assert(worker);
	assert(worker->tid != 0);
	assert(worker != current_worker);
	assert(cpu);

	worker->new_cpu = cpu;

	// if we are waking up a thread of a different process, keep
	// track of the new running process in the cpumanager
	if (worker->logic_pid != logic_pid)
		cpu_set_pid(cpu, worker->logic_pid);

	// if the worker was not previously bound to the core where it
	// is going to wake up now, bind it remotely now
	if (worker->cpu != cpu) {
		instr_affinity_remote(cpu->logic_id, worker->tid);
		if (unlikely(bypass_sched_setaffinity(worker->tid, sizeof(cpu->cpuset), &cpu->cpuset)))
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
	nosv_condvar_init(&threadmanager->condvar);
	atomic_init(&threadmanager->leader_shutdown_cpu, -1);
	threadmanager->delegate_joined = 0;

	current_process_manager = threadmanager;

	delegate_thread_create(threadmanager);
}

static inline nosv_worker_t *get_idle_worker(thread_manager_t *threadmanager)
{
	nosv_spin_lock(&threadmanager->idle_spinlock);
	list_head_t *head = list_pop_front(&threadmanager->idle_threads);
	nosv_spin_unlock(&threadmanager->idle_spinlock);
	return (head) ? list_elem(head, nosv_worker_t, list_hook) : NULL;
}

static void killer_task_run_callback(nosv_task_t task)
{
	// Unregister this process, and make it available
	pidmanager_unregister();

	// The core running this task becomes the leader shutdown cpu
	assert(current_worker);
	assert(current_worker->cpu);
	assert(current_process_manager);
	atomic_store_explicit(&current_process_manager->leader_shutdown_cpu, current_worker->cpu->logic_id, memory_order_relaxed);

	// Ask the delegate thread to shutdown as well
	creation_event_t event;
	event.type = Shutdown;
	event_queue_put(&current_process_manager->thread_creation_queue, &event);

	// Threads should shutdown at this moment
	atomic_store_explicit(&threads_shutdown_signal, 1, memory_order_relaxed);

	// Notify the scheduler to release all threads corresponding to this process since
	// we are shutting down
	scheduler_wake(logic_pid);
}

void threadmanager_shutdown(thread_manager_t *threadmanager)
{
	assert(threadmanager);
	// This should happen outside of a worker
	assert(worker_current() == NULL);

	// This thread coordinates the shutdown of all workers in this process.
	// The objective is to alert all active workers to exit the scheduler
	// and finish the idle workers. To end the idle workers in a coordinated
	// manner we need to give them a core to run. To schedule idle workers to
	// cores we need an active worker (a worker that owns a core) to
	// relinquish its own core in favor of the idle thread. However, it
	// could happen that all cores are owned by workers that belong to
	// another process. Therefore, our idle workers have no chance of being
	// scheduled. To fix this, we spawn a special task called "killer" task,
	// that will force a worker of this process to run it.  At the same
	// turn, the code of the killer task signals this process to start the
	// shutdown. This is needed to ensure that the killer task is run by
	// somebody before the shutdown starts.

	// Create killer task
	nosv_task_t killer_task;
	nosv_task_type_t type;
	int ret = nosv_type_init(&type, killer_task_run_callback, NULL, NULL, "killer", NULL, NULL, NOSV_TYPE_INIT_EXTERNAL);
	if (ret != NOSV_SUCCESS) {
		nosv_abort("Error: Cannot create killer task type\n");
	}
	ret = nosv_create(&killer_task, type, 0, NOSV_CREATE_NONE);
	if (ret != NOSV_SUCCESS) {
		nosv_abort("Error: Cannot create killer task\n");
	}

	// Submit killer task
	ret = nosv_submit(killer_task, NOSV_SUBMIT_NONE);
	if (ret != NOSV_SUCCESS) {
		nosv_abort("Error: Cannot submit killer task\n");
	}

	// wait until all worker threads have finished
	nosv_condvar_wait(&threadmanager->condvar);

	// Ensure that no more threads have been created
	char join;
	nosv_spin_lock(&current_process_manager->shutdown_spinlock);
	size_t destroyed = clist_count(&current_process_manager->shutdown_threads);
	int threads = atomic_load_explicit(&current_process_manager->created, memory_order_acquire);
	join = (threads == destroyed);
	assert(threads >= destroyed);
	nosv_spin_unlock(&current_process_manager->shutdown_spinlock);
	if (!join)
		nosv_abort("Error: Shutdown failed to take down all worker threads.\n");

	// Free killer task structures
	ret = nosv_destroy(killer_task, NOSV_DESTROY_NONE);
	if (ret != NOSV_SUCCESS) {
		nosv_abort("Error: Cannot destroy the killer task\n");
	}
	ret = nosv_type_destroy(type, NOSV_TYPE_DESTROY_NONE);
	if (ret != NOSV_SUCCESS) {
		nosv_abort("Error: Cannot destroy the killer task' type\n");
	}

	// We don't really need locking anymore
	list_head_t *head;
	clist_for_each_pop(head, &threadmanager->shutdown_threads) {
		nosv_worker_t *worker = list_elem(head, nosv_worker_t, list_hook);
		worker_join(worker);
		nosv_condvar_destroy(&worker->condvar);
		sfree(worker, sizeof(nosv_worker_t), -1);
	}

	event_queue_destroy(&threadmanager->thread_creation_queue);

	threadmanager = NULL;
}

static inline void worker_coordinate_shutdown(void)
{
	char join = 0;
	nosv_worker_t *idle_worker;

	// Add this worker to the list of workers to be freed
	nosv_spin_lock(&current_process_manager->shutdown_spinlock);
	clist_add(&current_process_manager->shutdown_threads, &current_worker->list_hook);
	nosv_spin_unlock(&current_process_manager->shutdown_spinlock);

	// Figure out if this is the shutdown leader core.
	int leader_id = atomic_load_explicit(&current_process_manager->leader_shutdown_cpu, memory_order_relaxed);
	char leader_shutdown_cpu = current_worker->cpu->logic_id == leader_id;

	// This core participates in a coordinated effort to shutdown all idle
	// workers. To do so, the current worker will attempt to wake an idle
	// worker in this core. If the wake is successful, the current worker
	// exits this function, the thread ends, and the woken worker repeats
	// the same procedure once it reaches this function.
	//
	// However, if the wake if not successful, we need to distinguish
	// between the regular and the leader core roles. A worker in a regular
	// core simply transfers the core ownership to another active process
	// and exits. Instead, a worker in the leader shutdown core loops until
	// it sees that all workers have reached this function. This is needed
	// to counter races of workers in a transitive state (they are about to
	// enter the idle list or the dtlock). If the leader finds another idle
	// thread, it wakes it and exits, allowing the awoken worker to run in
	// the leader core. If the leader finds out that all workers are done,
	// it notifies the main thread coordinating the shutdown and exits.
retry:
	idle_worker = get_idle_worker(current_process_manager);
	if (idle_worker) {
		instr_affinity_set(-1);
		worker_wake_internal(idle_worker, current_worker->cpu);
	} else {
		if (leader_shutdown_cpu) {
			// The leader shutdown core remains active until all
			// other workers have finished.

			// Try to join the delegation thread
			if (!current_process_manager->delegate_joined) {
				int ret = pthread_tryjoin_np(current_process_manager->delegate_thread, NULL);
				if (ret == 0) {
					current_process_manager->delegate_joined = 1;
				} else if (ret != EBUSY) {
					nosv_abort("Error: Joining delegation thread");
				}
			}

			// If the delegate has joined, check if all workers have been joined
			if (current_process_manager->delegate_joined) {
				nosv_spin_lock(&current_process_manager->shutdown_spinlock);
				size_t destroyed = clist_count(&current_process_manager->shutdown_threads);
				int threads = atomic_load_explicit(&current_process_manager->created, memory_order_acquire);
				join = (threads == destroyed);
				assert(threads >= destroyed);
				nosv_spin_unlock(&current_process_manager->shutdown_spinlock);
			}

			if (!join) {
				// We need to wait for other workers to end
				scheduler_wake(logic_pid);
				usleep(1000);
				goto retry;
			} else {
				// All workers have finished, notify the thread
				// that called "nosv_shutdown"
				nosv_condvar_signal(&current_process_manager->condvar);
			}
		}
		// This process is done with this core, transfer it to another pid
		pidmanager_transfer_to_idle(current_worker->cpu);
	}
}

static inline void worker_execute_or_delegate(task_execution_handle_t handle, cpu_t *cpu, int is_busy_worker)
{
	assert(handle.task);
	assert(handle.execution_id > 0);
	assert(cpu);

	nosv_task_t task = handle.task;

	if (task->worker != NULL) {
		// Another thread was already running the task, so we have to resume
		// the execution of the thread
		assert(!task_is_parallel(task));
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

		cpu_transfer(task->type->pid, cpu, handle);
		worker_block();
	} else if (is_busy_worker) {
		// The task has not started and it is from the current PID, but the
		// current worker is busy and cannot execute directly the task. Then
		// delegate the work and wake up an idle thread from this PID to
		// execute the task
		instr_thread_cool();
		worker_wake_idle(logic_pid, cpu, handle);
		worker_block();
	} else {
		// Otherwise, start running the task because the current thread is
		// valid to run the task and it is idle
		task_execute(handle);
		// The task execution has ended, so do not block the thread
	}
}

static inline void *worker_start_routine(void *arg)
{
	uint64_t timestamp;
	int pid;

	current_worker = (nosv_worker_t *)arg;
	assert(current_worker);
	assert(current_worker->cpu);
	cpu_set_current(current_worker->cpu->logic_id);
	current_worker->tid = gettid();
	pid = current_worker->logic_pid;
	assert(current_process_manager);

	// Initialize hardware counters for the thread
	hwcounters_thread_initialize(current_worker);

	// Register thread in affinity support subsystem
	affinity_support_register_worker(current_worker, 1);

	// Configure turbo
	arch_configure_turbo(nosv_config.turbo_enabled);

	instr_thread_init();
	instr_thread_execute(current_worker->cpu->logic_id, current_worker->creator_tid, (uint64_t) arg);
	instr_worker_enter();
	instr_kernel_init(&kinstr);

	// At the initialization, we signal the instrumentation to state
	// that we are looking for work.
	instr_sched_hungry();

	while (!atomic_load_explicit(&threads_shutdown_signal, memory_order_relaxed)) {
		task_execution_handle_t handle = current_worker->handle;
		int cpu = cpu_get_current();

		if (!handle.task && current_worker->immediate_successor) {
			assert(!task_is_parallel(current_worker->immediate_successor));

			// Check if our quantum is up to prevent hoarding CPU resources. This is only
			// necessary here as quantum is already taken into account in "scheduler_get"
			if (scheduler_should_yield(pid, cpu, &timestamp)) {
				// To prevent immediate successor chains to be unnecessarily broken if no tasks are up,
				// try to obtain a new task first
				task_execution_handle_t candidate = scheduler_get(cpu, SCHED_GET_NONBLOCKING | SCHED_GET_EXTERNAL);

				if (candidate.task) {
					assert(candidate.task->type->pid != pid);
					scheduler_submit_single(current_worker->immediate_successor);
					handle = candidate;
				} else {
					handle.task = current_worker->immediate_successor;
					handle.execution_id = 1;
					// Reset the quantum
					scheduler_reset_accounting(pid, cpu);

				}
			} else {
				handle.task = current_worker->immediate_successor;
				handle.execution_id = 1;
			}

			worker_set_immediate(NULL);
		}

		if (!handle.task && current_worker->cpu)
			handle = scheduler_get(cpu, SCHED_GET_DEFAULT);

		if (handle.task) {
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

			worker_execute_or_delegate(handle, current_worker->cpu, /* idle thread */ 0);

			instr_kernel_flush(kinstr);

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

	affinity_support_unregister_worker(current_worker, 0);

	hwcounters_update_runtime_counters();

	worker_coordinate_shutdown();
	// After this point, we no longer own the current core. However, the
	// worker struct will not be freed until the current thread is joined.

	hwcounters_thread_shutdown(current_worker);

	instr_worker_exit();
	instr_kernel_flush(kinstr);
	instr_thread_end();

	// Verify before finish that turbo config
	// has not been modified
	worker_check_turbo();

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
	task_execution_handle_t handle = EMPTY_TASK_EXECUTION_HANDLE;
	worker_wake_idle(logic_pid, current_worker->cpu, handle);

	// Then, sleep and return once we have been woken up
	worker_block();
}

void worker_yield_to(task_execution_handle_t handle)
{
	assert(current_worker);
	cpu_t *cpu = current_worker->cpu;
	assert(cpu);
	assert(handle.task);

	nosv_task_t current_task = worker_current_task();
	assert(current_task);
	assert(!task_is_parallel(current_task));

	uint32_t bodyid = instr_get_bodyid(handle);
	instr_task_pause((uint32_t)current_task->taskid, bodyid);

	// We have to submit the current task so it gets resumed somewhere else
	scheduler_submit_single(current_task);

	// Wake up the corresponding thread to execute the task
	worker_execute_or_delegate(handle, cpu, /* busy thread */ 1);

	instr_task_resume((uint32_t)current_task->taskid, bodyid);
}

int worker_yield_if_needed(nosv_task_t current_task)
{
	assert(current_worker);
	assert(current_worker->handle.task == current_task);
	assert(current_task->worker == current_worker);

	cpu_t *cpu = current_worker->cpu;
	assert(cpu);

	instr_sched_hungry();

	// Try to get a ready task without blocking
	task_execution_handle_t handle = scheduler_get(cpu->logic_id, SCHED_GET_NONBLOCKING);

	instr_sched_fill();

	if (!handle.task)
		return 0;

	worker_yield_to(handle);

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
	current_worker->cpu = current_worker->new_cpu;
	cpu_t *cpu = current_worker->cpu;

	assert(cpu);
	cpu_set_current(cpu->logic_id);

	instr_thread_resume();
}

static inline void worker_create_remote(thread_manager_t *threadmanager, cpu_t *cpu, task_execution_handle_t handle)
{
	creation_event_t event;
	event.cpu = cpu;
	event.handle = handle;
	event.type = Creation;

	event_queue_put(&threadmanager->thread_creation_queue, &event);
}

void worker_wake_idle(int pid, cpu_t *cpu, task_execution_handle_t handle)
{
	// Find the remote thread manager
	thread_manager_t *threadmanager = pidmanager_get_threadmanager(pid);
	assert(threadmanager);

	nosv_spin_lock(&threadmanager->idle_spinlock);
	list_head_t *head = list_pop_front(&threadmanager->idle_threads);
	nosv_spin_unlock(&threadmanager->idle_spinlock);

	if (head) {
		// We have an idle thread to wake up
		nosv_worker_t *worker = list_elem(head, nosv_worker_t, list_hook);
		assert(!worker->handle.task);
		worker->handle = handle;
		worker_wake_internal(worker, cpu);
		return;
	}

	// We need to create a new worker.
	if (pid == logic_pid) {
		worker_create_local(threadmanager, cpu, handle);
	} else {
		worker_create_remote(threadmanager, cpu, handle);
	}
}

nosv_worker_t *worker_create_local(thread_manager_t *threadmanager, cpu_t *cpu, task_execution_handle_t handle)
{
	atomic_fetch_add_explicit(&threadmanager->created, 1, memory_order_release);
	assert(cpu);

	nosv_worker_t *worker = (nosv_worker_t *)salloc(sizeof(nosv_worker_t), cpu_get_current());
	worker->cpu = cpu;
	worker->handle = handle;
	worker->logic_pid = logic_pid;
	worker->immediate_successor = NULL;
	worker->creator_tid = gettid();
	worker->in_task_body = 0;
	nosv_condvar_init(&worker->condvar);
	list_init(&worker->list_hook);

	// We use the address of the worker structure as the tag of the
	// thread create event, as it provides a unique value known to
	// both threads.
	instr_thread_create(cpu->logic_id, (uint64_t) worker);

	common_pthread_create(&worker->kthread, worker_start_routine, worker, &cpu->cpuset);

	return worker;
}

nosv_worker_t *worker_create_external(void)
{
	nosv_worker_t *worker = (nosv_worker_t *)salloc(sizeof(nosv_worker_t), cpu_get_current());
	worker->cpu = NULL;
	worker->handle = EMPTY_TASK_EXECUTION_HANDLE;
	worker->kthread = pthread_self();
	worker->tid = gettid();
	worker->logic_pid = logic_pid;
	nosv_condvar_init(&worker->condvar);
	current_worker = worker;
	worker->immediate_successor = NULL;
	worker->creator_tid = -1;
	worker->in_task_body = 1;
	worker->original_affinity = NULL;
	worker->original_affinity_size = 0;
	list_init(&worker->list_hook);

	instr_kernel_init(&kinstr);

	// Initialize hardware counters for the thread
	hwcounters_thread_initialize(worker);

	// Configure turbo
	arch_configure_turbo(nosv_config.turbo_enabled);

	return worker;
}

void worker_free_external(nosv_worker_t *worker)
{
	assert(worker);
	assert(worker == current_worker);

	// Initialize hardware counters for the thread
	hwcounters_thread_shutdown(worker);

	nosv_condvar_destroy(&worker->condvar);
	sfree(worker, sizeof(nosv_worker_t), cpu_get_current());

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

	if (!current_worker->handle.task)
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

	return current_worker->handle.task;
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

void worker_check_turbo(void)
{
	if (arch_check_turbo(nosv_config.turbo_enabled)) {
		if (nosv_config.turbo_enabled) {
			nosv_abort("Found inconsistency between nOS-V turbo config setting and the thread configuration\n"
				"Turbo is enabled in nOS-V configuration, but in the worker thread it is not.\n"
				"This usually means the user's code has manually disabled it.");
		} else {
			nosv_abort("Found inconsistency between nOS-V turbo config setting and the thread configuration\n"
				"Turbo is disabled in nOS-V configuration, but in the worker thread it is.\n"
				"This usually means the user's code has been compiled with -ffast-math or similar.");
		}
	}
}
