/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <stdint.h>

#include <nosv/memory.h>

#include "memory/backbone.h"

#define align_to(n, x) (((x) + (n) - 1) & ~((n) - 1))

void *backbone_pages_start;
page_metadata_t *backbone_metadata_start;
static size_t backbone_size;
static size_t backbone_pages;
backbone_header_t *backbone_header;

// Type of a whole page, for pointer arithmetic purposes.
typedef struct meta_page {
	char x[PAGE_SIZE];
} page_t;

void backbone_alloc_init(void *start, size_t size, int initialize)
{
	backbone_size = size;

	// How many pages can we store?
	backbone_pages = (backbone_size - (sizeof(backbone_header_t))) / (sizeof(page_metadata_t) + PAGE_SIZE);
	// This is optimistic because we need to pad the first page to be aligned. We may loose at most one page to padding.

	backbone_metadata_start = (page_metadata_t *)(((uintptr_t)start) + sizeof(backbone_header_t));
	// Has to be aligned to 16 bytes because of double compare and exchange reasons
	uintptr_t metadata_start = (uintptr_t)backbone_metadata_start;
	metadata_start = align_to(16, metadata_start);
	backbone_metadata_start = (void *)metadata_start;

	uintptr_t pages_start = (uintptr_t)&backbone_metadata_start[backbone_pages];
	// Align pages
	pages_start = align_to(PAGE_SIZE, pages_start);
	backbone_pages_start = (void *)pages_start;
	// Re-compute the number of pages based on the needed padding

	uint64_t space_left = (uint64_t)((((char *)start) + backbone_size) - pages_start);
	backbone_pages = space_left / PAGE_SIZE;
	backbone_header = (backbone_header_t *)start;

	// If we're not the first process, this is enough.
	if (!initialize)
		return;

	clist_init(&backbone_header->free_pages);

	page_t *page = (page_t *)pages_start;

	for (size_t i = 0; i < backbone_pages; ++i) {
		list_init(&backbone_metadata_start[i].list_hook);
		clist_add(&backbone_header->free_pages, &(backbone_metadata_start[i].list_hook));
		backbone_metadata_start[i].addr = (void *)page;
#ifndef ARCH_HAS_DWCAS
		nosv_spin_init(&backbone_metadata_start[i].lock);
#endif
		page++;
	}

	nosv_sys_mutex_init(&backbone_header->mutex);
}

page_metadata_t *balloc(void)
{
	page_metadata_t *ret = NULL;
	nosv_sys_mutex_lock(&backbone_header->mutex);

	list_head_t *first = clist_pop_front(&backbone_header->free_pages);

	if (first) {
		ret = list_elem(first, page_metadata_t, list_hook);
	}

	nosv_sys_mutex_unlock(&backbone_header->mutex);

	return ret;
}

void bfree(page_metadata_t *block)
{
	nosv_sys_mutex_lock(&backbone_header->mutex);

	clist_add(&backbone_header->free_pages, &block->list_hook);

	nosv_sys_mutex_unlock(&backbone_header->mutex);
}

// Implementation of public API functions to gauge memory pressure

static inline size_t backbone_used_memory(void)
{
	// Total size subtracting the free pages (this already accounts for headers)
	return backbone_size - clist_count(&backbone_header->free_pages) * PAGE_SIZE;
}

int nosv_memory_get_used(size_t *used)
{
	if (!used)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!backbone_header)
		return NOSV_ERR_NOT_INITIALIZED;

	*used = backbone_used_memory();
	return NOSV_SUCCESS;
}

int nosv_memory_get_size(size_t *size)
{
	if (!size)
		return NOSV_ERR_INVALID_PARAMETER;

	*size = backbone_size;
	return NOSV_SUCCESS;
}

int nosv_memory_get_pressure(float *pressure)
{
	if (!pressure)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!backbone_header)
		return NOSV_ERR_NOT_INITIALIZED;

	*pressure = ((float) backbone_used_memory()) / ((float) backbone_size);
	return NOSV_SUCCESS;
}
