/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024-2025 Barcelona Supercomputing Center (BSC)
*/

#include <stdlib.h>

#include "compiler.h"
#include "common.h"

void slab_init(void)
{
}

typedef struct salloc_asan {
	size_t size;
	char addr[] __cacheline_aligned;
} *salloc_asan_t;

void *salloc(size_t size, __maybe_unused int cpu)
{
	salloc_asan_t ptr = malloc(sizeof(struct salloc_asan) + size);
	ptr->size = size;
	return &ptr->addr;
}

void sfree(void *ptr, size_t size, __maybe_unused int cpu)
{
	salloc_asan_t real_ptr = (salloc_asan_t)((char*)ptr - sizeof(struct salloc_asan));

	if (size != real_ptr->size) {
		nosv_warn("Detected sfree of pointer %p with wrong size: got=%zu expected=%zu", ptr, size, real_ptr->size);
		free(real_ptr); // Double free so that asan reports the error
	}

	free(real_ptr);
}
