/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef BACKBONE_H
#define BACKBONE_H

#include <stddef.h>
#include <stdint.h>

#include "generic/mutex.h"
#include "generic/list.h"
#include "memory/slab.h"

// 2MB Pages
#define PAGE_SIZE (1 << 21)

// Some of the members of this struct are for the slab allocator.
struct page_metadata;

typedef struct __attribute__((aligned(16))) page_metadata {
	void *freelist;
	uint64_t inuse_chunks;
#ifndef ARCH_HAS_DWCAS
	nosv_spinlock_t lock;
#endif
	void *addr;
	list_head_t list_hook;
} page_metadata_t;

typedef struct backbone_header {
	list_head_t free_pages;
	nosv_mutex_t mutex;
	cache_bucket_t buckets[SLAB_BUCKETS];
} backbone_header_t;

__internal extern void *backbone_pages_start;
__internal extern page_metadata_t *backbone_metadata_start;
__internal extern backbone_header_t *backbone_header;

__internal void backbone_alloc_init(void *start, size_t size, int initialize);

__internal page_metadata_t *balloc();
__internal void bfree(page_metadata_t *block);

static inline page_metadata_t *page_metadata_from_block(void *block)
{
	size_t block_idx = (((uintptr_t)block) - ((uintptr_t)backbone_pages_start)) / PAGE_SIZE;
	return &backbone_metadata_start[block_idx];
}

#endif // BUDDY_H
