/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef SLAB_H
#define SLAB_H

#include <stddef.h>
#include <stdint.h>

#include "compiler.h"
#include "list.h"
#include "spinlock.h"

#define SLAB_ALLOC_MIN 3
#define SLAB_BUCKETS (20 - SLAB_ALLOC_MIN)

#define NR_CPUS 512

struct page_metadata;

typedef struct cpu_cache_bucket {
	struct page_metadata *slab;
	void *freelist;
} cpu_cache_bucket_t;

typedef struct cache_bucket {
	size_t obj_size;
	clist_head_t free;
	clist_head_t partial;
	nosv_spinlock_t lock;

	cpu_cache_bucket_t cpubuckets[NR_CPUS];
} cache_bucket_t;

__internal void *salloc(size_t size, int cpu);
__internal void sfree(void *ptr, size_t size, int cpu);
__internal void slab_init();

#endif // SLAB_H