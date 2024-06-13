/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_MEMORY_H
#define NOSV_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#include "error.h"

/*
   Memory allocator information routines for nOS-V.
   These functions can be used to retrieve estimates of memory usage and pressure in the nOS-V shared
   memory segment, and thus can be used to implement throttling policies based on the shared memory left.
   These functions can be used outside a task context, but assume nOS-V is initialized.
*/

/* Gets an upper bound of the bytes used in the shared memory segment */
int nosv_memory_get_used(size_t *used);

/* Gets the total shared memory size in bytes */
int nosv_memory_get_size(size_t *size);

/* Gets the memory pressure, from 0 (no pressure) to 1 (full memory) */
int nosv_memory_get_pressure(float *pressure);

#ifdef __cplusplus
}
#endif

#endif // NOSV_MEMORY_H
