/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef BACKBONE_H
#define BACKBONE_H

#include <stddef.h>
#include <stdint.h>

// Some of the members of this struct are for the slab allocator.
struct page_metadata;

typedef struct page_metadata {
	struct page_metadata *next;
	struct page_metadata *prev;
	void *freelist;
	uint16_t inuse_chunks;
	void *addr;
} page_metadata_t;

typedef struct backbone_header {
	page_metadata_t *free_pages;

} backbone_header_t;

void backbone_alloc_init(void *start, size_t size, size_t blocksize);

void *balloc();
void bfree(void *block);

#endif // BUDDY_H