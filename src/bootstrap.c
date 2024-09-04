/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>

#include "common.h"
#include "compiler.h"
#include "config/config.h"
#include "hardware/locality.h"
#include "hardware/pids.h"
#include "hardware/threads.h"
#include "hwcounters/hwcounters.h"
#include "instr.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"
#include "monitoring/monitoring.h"
#include "nosv.h"
#include "scheduler/scheduler.h"
#include "support/affinity.h"
#include "system/tasks.h"

__internal int32_t rt_refcount = 0;
__internal thread_local int32_t th_refcount = 0;
__internal bool rt_initialized = false;
__internal pthread_mutex_t rt_mutex = PTHREAD_MUTEX_INITIALIZER;

static void configure_fork_hooks(void);

//! \brief Perform the runtime initialization if required
static int nosv_init_impl(void)
{
	if (rt_refcount < 0)
		return NOSV_ERR_UNKNOWN;

	// The first thread initializes the runtime
	if (++rt_refcount == 1) {
		config_parse();
		affinity_support_init();
		locality_init();
		smem_initialize();
		hwcounters_initialize();
		pidmanager_register();
		task_type_manager_init();
		task_affinity_init();
		configure_fork_hooks();

		// Mark as initialized
		assert(th_refcount == 0);
		th_refcount++;
		rt_initialized = true;
	} else if (++th_refcount == 1 && !nosv_self()) {
		// Emit instrumentation if the current thread is the first time
		// that this thread calls nosv_init and it is not attached.
		assert(rt_initialized);

		instr_thread_init();
		instr_thread_execute(-1, -1, 0);
	}

	return 0;
}

//! \brief Perform the runtime shutdown if required
static int nosv_shutdown_impl(void)
{
	if (rt_refcount <= 0)
		return NOSV_ERR_UNKNOWN;

	// The last thread finalizes the runtime
	if (--rt_refcount == 0) {
		pidmanager_shutdown();
		scheduler_shutdown();

		// Display reports of statistics before deleting task types
		monitoring_display_stats();

		task_type_manager_shutdown();
		smem_shutdown();

		// Shutdown hwcounters after the shared memory, as this latter
		// can finalize modules that need hwcounters (e.g., monitoring)
		hwcounters_shutdown();

		affinity_support_shutdown();
		locality_shutdown();
		config_free();

		assert(th_refcount == 1);
		th_refcount--;
		rt_initialized = false;

		instr_thread_end();
		instr_proc_fini();
	} else if (--th_refcount == 0 && !nosv_self()) {
		// Emit instrumentation if the current thread is the last thread
		// calling nosv_shutdown and it is not attached.
		instr_thread_end();
	}
	return NOSV_SUCCESS;
}

int nosv_init(void)
{
	pthread_mutex_lock(&rt_mutex);
	int res = nosv_init_impl();
	pthread_mutex_unlock(&rt_mutex);
	return res;
}

int nosv_shutdown(void)
{
	pthread_mutex_lock(&rt_mutex);
	int res = nosv_shutdown_impl();
	pthread_mutex_unlock(&rt_mutex);
	return res;
}

void __constructor __nosv_construct_library(void)
{
}

void __destructor __nosv_destruct_library(void)
{
	if (rt_refcount > 0) {
		nosv_warn("nosv_shutdown() was not called to correctly shutdown the library.");
	}
}

static void child_fork_hook(void)
{
	// In case of a process which has initialized nOS-V and then is forked, we have to behave
	// as if the child was never initialized, in order to allow it to initialize at a later point
	// if need be.

	if (rt_initialized) {
		rt_initialized = false;
		rt_refcount = 0;
		th_refcount = 0;

		munmap(nosv_config.shm_start, nosv_config.shm_size);
	}
}

static void configure_fork_hooks(void)
{
	pthread_atfork(NULL, NULL, child_fork_hook);
}
