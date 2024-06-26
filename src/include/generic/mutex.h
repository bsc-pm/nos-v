/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef MUTEX_H
#define MUTEX_H

#include <pthread.h>

/*
	Lightweight wrapper for pthread_mutex_t with shared memory support
*/

typedef pthread_mutex_t nosv_sys_mutex_t;

static inline void nosv_sys_mutex_init(nosv_sys_mutex_t *mutex)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(mutex, &attr);
	pthread_mutexattr_destroy(&attr);
}

static inline void nosv_sys_mutex_destroy(nosv_sys_mutex_t *mutex)
{
	pthread_mutex_destroy(mutex);
}

static inline void nosv_sys_mutex_lock(nosv_sys_mutex_t *mutex)
{
	pthread_mutex_lock(mutex);
}

static inline void nosv_sys_mutex_unlock(nosv_sys_mutex_t *mutex)
{
	pthread_mutex_unlock(mutex);
}

#endif // MUTEX_H
