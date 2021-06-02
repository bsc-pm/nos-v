/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <pthread.h>

/*
	Lightweight wrapper for pthread_spinlock_t with shared memory support
*/

typedef pthread_spinlock_t nosv_spinlock_t;

static inline void nosv_spin_init(nosv_spinlock_t *spinlock)
{
	pthread_spin_init(spinlock, 1);
}

static inline void nosv_spin_destroy(nosv_spinlock_t *spinlock)
{
	pthread_spin_destroy(spinlock);
}

static inline void nosv_spin_lock(nosv_spinlock_t *spinlock)
{
	pthread_spin_lock(spinlock);
}

static inline void nosv_spin_unlock(nosv_spinlock_t *spinlock)
{
	pthread_spin_unlock(spinlock);
}

#endif // SPINLOCK_H