/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef BACKBONE_H
#define BACKBONE_H

#include <stddef.h>

void backbone_alloc_init(void *start, size_t size, size_t blocksize);

void *balloc();
void bfree(void *block);

#endif // BUDDY_H