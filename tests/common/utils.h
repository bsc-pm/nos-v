/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>

#define CHECK(f...)                                                                \
do {                                                                               \
	const int __r = f;                                                             \
	if (__r) {                                                                     \
		fprintf(stderr, "Error: '%s' [%s:%i]: %i\n", #f, __FILE__, __LINE__, __r); \
		exit(EXIT_FAILURE);                                                        \
	}                                                                              \
} while (0)

#endif // UTILS_H
