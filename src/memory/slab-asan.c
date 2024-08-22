/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#include <stdlib.h>

#include "compiler.h"

void slab_init(void)
{
}

void *salloc(size_t size, __maybe_unused int cpu)
{
	return malloc(size);
}

void sfree(void *ptr, __maybe_unused size_t size, __maybe_unused int cpu)
{
	free(ptr);
}
