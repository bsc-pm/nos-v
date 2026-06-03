/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2026 Barcelona Supercomputing Center (BSC)
*/

#ifndef CONDVAR_H
#define CONDVAR_H

#include <assert.h>
#include <errno.h>
#include <semaphore.h>

#include "compiler.h"

typedef struct condition_var {
	sem_t sem;
} nosv_condvar_t;

static inline void nosv_condvar_init(nosv_condvar_t *condvar)
{
	__maybe_unused int res;

	assert(condvar);
	res = sem_init(&condvar->sem, 1, 0);
	assert(!res);
}

static inline void nosv_condvar_destroy(nosv_condvar_t *condvar)
{
	__maybe_unused int res;

	assert(condvar);
	res = sem_destroy(&condvar->sem);
	assert(!res);
}

static inline void nosv_condvar_wait(nosv_condvar_t *condvar)
{
	int res;

	assert(condvar);
	while ((res = sem_wait(&condvar->sem)) && errno == EINTR) {}
	assert(!res);
}

static inline void nosv_condvar_signal(nosv_condvar_t *condvar)
{
	__maybe_unused int res;

	assert(condvar);
	assert(sem_getvalue(&condvar->sem, &res) || res == 0);
	res = sem_post(&condvar->sem);
	assert(!res);
}

#endif // CONDVAR_H
