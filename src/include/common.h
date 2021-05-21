/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef COMMON_H
#define COMMON_H

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

static inline void nosv_abort(const char *msg)
{
	fprintf(stderr, "NOS-V ERROR: %s\n", msg);
	perror("Last Error");
	fprintf(stderr, "Aborting\n");
	exit(1);
}

static inline void nosv_warn(const char *msg)
{
	fprintf(stderr, "NOS-V WARNING: %s\n", msg);
}

#endif // COMMON_H