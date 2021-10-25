#ifndef COMPAT_H
#define COMPAT_H

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <sys/syscall.h>
#include <unistd.h>

// Define gettid for older glibc versions (below 2.30)
#if !__GLIBC_PREREQ(2, 30)
static inline pid_t gettid(void)
{
	return (pid_t) syscall(SYS_gettid);
}
#endif

#endif // COMPAT_H
