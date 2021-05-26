/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include "memory/backbone.h"

static void *backbone_start;
static size_t backbone_blocksize;
static size_t backbone_size;
static size_t backbone_pages;

void backbone_alloc_init(void *start, size_t size, size_t blocksize)
{
	backbone_start = start;
	backbone_size = size;
	backbone_blocksize = blocksize;

	// How many pages can we store?
	backbone_pages = (backbone_size - )
}

void *balloc()
{

}

void bfree(void *block)
{

}
