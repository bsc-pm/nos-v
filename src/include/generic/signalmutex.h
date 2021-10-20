/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef SIGNALMUTEX_H
#define SIGNALMUTEX_H

#include <assert.h>
#include <pthread.h>

#include "compiler.h"

typedef struct signal_mutex {
	pthread_mutex_t mutex;
	pthread_cond_t condvar;
} nosv_signal_mutex_t;

static inline void nosv_signal_mutex_init(nosv_signal_mutex_t *smutex)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;
	__maybe_unused int res;

	assert(smutex);

	res = pthread_mutexattr_init(&mattr);
	assert(!res);
	res = pthread_condattr_init(&cattr);
	assert(!res);

	res = pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	assert(!res);
	res = pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
	assert(!res);

	res = pthread_mutex_init(&smutex->mutex, &mattr);
	assert(!res);
	res = pthread_cond_init(&smutex->condvar, &cattr);
	assert(!res);

	pthread_mutexattr_destroy(&mattr);
	pthread_condattr_destroy(&cattr);
}

static inline void nosv_signal_mutex_destroy(nosv_signal_mutex_t *smutex)
{
	assert(smutex);
	pthread_mutex_destroy(&smutex->mutex);
	pthread_cond_destroy(&smutex->condvar);
}

// Must be called with lock held
static inline void nosv_signal_mutex_wait(nosv_signal_mutex_t *smutex)
{
	pthread_cond_wait(&smutex->condvar, &smutex->mutex);
}

static inline void nosv_signal_mutex_signal(nosv_signal_mutex_t *smutex)
{
	pthread_cond_signal(&smutex->condvar);
}

static inline void nosv_signal_mutex_lock(nosv_signal_mutex_t *smutex)
{
	pthread_mutex_lock(&smutex->mutex);
}

static inline void nosv_signal_mutex_unlock(nosv_signal_mutex_t *smutex)
{
	pthread_mutex_unlock(&smutex->mutex);
}


#endif // SIGNALMUTEX_H
