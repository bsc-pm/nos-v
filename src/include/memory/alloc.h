/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef ALLOC_H
#define ALLOC_H

#include "compiler.h"

#include <stddef.h>

// Shared Memory allocation API
__internal void *smalloc(size_t size, int cpuId);

__internal void sfree(void *ptr, size_t size, int cpuId);

#endif // ALLOC_H