/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include "compiler.h"
#include "memory/sharedmemory.h"

void __constructor __nosv_construct_library(void)
{
	smem_initialize();
}

void __destructor __nosv_destruct_library(void)
{
	smem_shutdown();
}