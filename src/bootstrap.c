/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include "compiler.h"
#include "hardware/threads.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"

void __constructor __nosv_construct_library(void)
{
	smem_initialize();
	void *test = salloc(1024, 0);
	sfree(test, 1024, 0);

	threadmanager_init();
}

void __destructor __nosv_destruct_library(void)
{
	smem_shutdown();
}