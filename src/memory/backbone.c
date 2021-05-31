/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <stdint.h>

#include "memory/backbone.h"

void *backbone_pages_start;
page_metadata_t *backbone_metadata_start;
static size_t backbone_size;
static size_t backbone_pages;
static backbone_header_t *backbone_header;

// Type of a whole page, for pointer arithmetic purposes.
typedef struct meta_page {
	char x[PAGE_SIZE];
} page_t;

void backbone_alloc_init(void *start, size_t size)
{
	backbone_size = size;

	// How many pages can we store?
	backbone_pages = (backbone_size - (sizeof(backbone_header_t))) / (sizeof(page_metadata_t) + PAGE_SIZE);
	// This is optimistic because we need to pad the first page to be aligned. We may loose at most one page to padding.

	backbone_metadata_start = (page_metadata_t *)(((uintptr_t)start) + sizeof(backbone_header_t));

	uintptr_t pages_start = (uintptr_t)&backbone_metadata_start[backbone_pages];
	// Align pages
	pages_start = (pages_start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	backbone_pages_start = (void *)pages_start;
	// Re-compute the number of pages based on the needed padding

	uint64_t space_left = (uint64_t)((((char *)start) + backbone_size) - pages_start);
	assert((space_left % PAGE_SIZE) == 0);
	backbone_pages = space_left / PAGE_SIZE;

	page_t *page = (page_t *)pages_start;

	for (size_t i = 0; i < backbone_pages; ++i) {
		if (i > 0)
			backbone_metadata_start[i].prev = &backbone_metadata_start[i - 1];
		else
			backbone_metadata_start[i].prev = NULL;

		if (i < backbone_pages - 1)
			backbone_metadata_start[i].next = &backbone_metadata_start[i + 1];
		else
			backbone_metadata_start[i].next = NULL;

		backbone_metadata_start[i].addr = (void *)page;

		page++;
	}

	backbone_header = (backbone_header_t *)start;

	backbone_header->free_pages = backbone_metadata_start;
	nosv_mutex_init(&backbone_header->mutex);
}

page_metadata_t *balloc()
{
	void *ret = NULL;
	nosv_mutex_lock(&backbone_header->mutex);

	if (backbone_header->free_pages != NULL) {
		ret = backbone_header->free_pages;
		backbone_header->free_pages = backbone_header->free_pages->next;

		if (backbone_header->free_pages != NULL)
			backbone_header->free_pages->prev = NULL;
	}

	nosv_mutex_unlock(&backbone_header->mutex);

	return ret;
}

void bfree(page_metadata_t *block)
{
	nosv_mutex_lock(&backbone_header->mutex);

	page_metadata_t *old = backbone_header->free_pages;
	backbone_header->free_pages = block;
	block->next = old;

	if (old)
		old->prev = block;

	nosv_mutex_unlock(&backbone_header->mutex);
}
