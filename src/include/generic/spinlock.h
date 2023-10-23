/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <pthread.h>
#include <stdatomic.h>

#include "generic/arch.h"


typedef atomic_int nosv_spinlock_t;

#define NOSV_SPINLOCK_INITIALIZER 0

static inline void nosv_spin_init(nosv_spinlock_t *spinlock)
{
	atomic_init(spinlock, 0);
}

static inline void nosv_spin_destroy(nosv_spinlock_t *spinlock)
{
}

static inline void nosv_spin_lock(nosv_spinlock_t *spinlock)
{
	int expected = 0;
	int desired = 1;

	if (atomic_compare_exchange_weak_explicit(spinlock, &expected, desired, memory_order_acquire, memory_order_relaxed))
		return;

	// contended
	do {
		do {
			spin_wait();
		} while (atomic_load_explicit(spinlock, memory_order_relaxed));
		expected = 0;
	} while (!atomic_compare_exchange_weak_explicit(spinlock, &expected, desired, memory_order_acquire, memory_order_relaxed));
	spin_wait_release();
}

static inline void nosv_spin_unlock(nosv_spinlock_t *spinlock)
{
	atomic_store_explicit(spinlock, 0, memory_order_release);
}

#endif // SPINLOCK_H
