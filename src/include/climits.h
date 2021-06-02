/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef LIMITS_H
#define LIMITS_H

/*
	This file is intended to contain all the static limits that may need to be edited
	in some cases by nOS-V users.
*/

// Maximum number of CPUs
#define NR_CPUS 256

// Maximum number of concurrent nOS-V processes
#define MAX_PIDS 128

// Cacheline Size
#define CACHELINE_SIZE 64

#endif // LIMITS_H