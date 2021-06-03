/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <stdio.h>

#include "compiler.h"
#include "nosv.h"
#include "hardware/threads.h"
#include "hardware/pids.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"

int library_initialized = 0;

int nosv_init()
{
	if (library_initialized != 0)
		return 1;

	smem_initialize();
	pidmanager_register();

	library_initialized = 1;
	return 0;
}

int nosv_shutdown()
{
	if (library_initialized != 1)
		return 1;

	pidmanager_shutdown();
	smem_shutdown();

	library_initialized = 0;
	return 0;
}

void __constructor __nosv_construct_library(void)
{
}

void __destructor __nosv_destruct_library(void)
{
	if (library_initialized == 1) {
		fprintf(stderr, "Warning: nosv_shutdown() was not called to correctly shutdown the library.");
	}
}