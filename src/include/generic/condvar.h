/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef CONDVAR_H
#define CONDVAR_H

#include <assert.h>
#include <pthread.h>

#include "compiler.h"

typedef struct condition_var {
	pthread_mutex_t mutex;
	pthread_cond_t condvar;
	int signaled;
} nosv_condvar_t;

static inline void nosv_condvar_init(nosv_condvar_t *condvar)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;
	__maybe_unused int res;

	assert(condvar);

	res = pthread_mutexattr_init(&mattr);
	assert(!res);
	res = pthread_condattr_init(&cattr);
	assert(!res);

	res = pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	assert(!res);
	res = pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
	assert(!res);

	res = pthread_mutex_init(&condvar->mutex, &mattr);
	assert(!res);
	res = pthread_cond_init(&condvar->condvar, &cattr);
	assert(!res);

	condvar->signaled = 0;
}

static inline void nosv_condvar_destroy(nosv_condvar_t *condvar)
{
	assert(condvar);
	pthread_mutex_destroy(&condvar->mutex);
	pthread_cond_destroy(&condvar->condvar);
}

static inline void nosv_condvar_wait(nosv_condvar_t *condvar)
{
	assert(condvar);

	pthread_mutex_lock(&condvar->mutex);
	while (!condvar->signaled)
		pthread_cond_wait(&condvar->condvar, &condvar->mutex);

	condvar->signaled = 0;
	pthread_mutex_unlock(&condvar->mutex);
}

static inline void nosv_condvar_signal(nosv_condvar_t *condvar)
{
	pthread_mutex_lock(&condvar->mutex);
	assert(!condvar->signaled);
	condvar->signaled = 1;
	pthread_cond_signal(&condvar->condvar);

	pthread_mutex_unlock(&condvar->mutex);
}

#endif // CONDVAR_H