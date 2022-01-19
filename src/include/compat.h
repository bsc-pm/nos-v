/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef COMPAT_H
#define COMPAT_H

#include <sys/syscall.h>
#include <unistd.h>

// Define gettid for older glibc versions (below 2.30)
#if !__GLIBC_PREREQ(2, 30)
static inline pid_t gettid(void)
{
	return (pid_t)syscall(SYS_gettid);
}
#endif

#endif // COMPAT_H
