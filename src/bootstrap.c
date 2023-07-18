/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#include <pthread.h>
#include <stdio.h>

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
#include "system/tasks.h"

__internal int32_t rt_refcount = 0;
__internal bool rt_initialized = false;
__internal pthread_mutex_t rt_mutex = PTHREAD_MUTEX_INITIALIZER;

//! \brief Perform the runtime initialization if required
static int nosv_init_impl(void)
{
	// Re-initializing the runtime is not supported
	if (rt_refcount == 0 && rt_initialized)
		return -EINVAL;
	if (rt_refcount < 0)
		return -EINVAL;

	// The first thread initializes the runtime
	if (++rt_refcount == 1) {
		config_parse();
		locality_init();
		smem_initialize();
		hwcounters_initialize();
		pidmanager_register();
		task_type_manager_init();
		task_affinity_init();

		// Mark as initialized
		rt_initialized = true;
	} else {
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
		return -EAGAIN;

	// The last thread finalizes the runtime
	if (--rt_refcount == 0) {
		pidmanager_shutdown();
		scheduler_shutdown(logic_pid);

		// Display reports of statistics before deleting task types
		monitoring_display_stats();

		task_type_manager_shutdown();
		smem_shutdown();

		// Shutdown hwcounters after the shared memory, as this latter
		// can finalize modules that need hwcounters (e.g., monitoring)
		hwcounters_shutdown();

		locality_shutdown();
		config_free();

		instr_thread_end();
		instr_proc_fini();
	} else {
		instr_thread_end();
	}
	return 0;
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
