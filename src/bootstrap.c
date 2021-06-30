/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include <stdio.h>

#include "common.h"
#include "compiler.h"
#include "nosv.h"
#include "config/config.h"
#include "hardware/locality.h"
#include "hardware/pids.h"
#include "hardware/threads.h"
#include "hwcounters/hwcounters.h"
#include "instr.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"
#include "scheduler/scheduler.h"
#include "system/tasks.h"

__internal int library_initialized = 0;

int nosv_init(void)
{
	if (library_initialized != 0)
		return 1;

	config_parse();

	instr_proc_init();
	instr_thread_init();
	instr_thread_execute(-1, -1, 0);

	// Generate some events to measure the latency
	instr_gen_bursts();
	locality_init();
	smem_initialize();
	hwcounters_initialize();
	pidmanager_register();
	task_type_manager_init();

	library_initialized = 1;
	return 0;
}

int nosv_shutdown(void)
{
	if (library_initialized != 1)
		return 1;

	pidmanager_shutdown();
	scheduler_shutdown();
	hwcounters_shutdown();
	task_type_manager_shutdown();
	smem_shutdown();
	locality_shutdown();
	config_free();

	library_initialized = 0;

	instr_thread_end();

	instr_proc_fini();

	return 0;
}

void __constructor __nosv_construct_library(void)
{
}

void __destructor __nosv_destruct_library(void)
{
	if (library_initialized == 1) {
		nosv_warn("nosv_shutdown() was not called to correctly shutdown the library.");
	}
}
