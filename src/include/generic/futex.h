/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef FUTEXVAR_H
#define FUTEXVAR_H

#include <assert.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "compiler.h"

// This file is highly linux-specific, but one could change struct nosv_futex_t to another type,
// for example a condition variable or a semaphore, in another OS

typedef struct futex {
	atomic_int32_t memory;
} nosv_futex_t;

// Define the futex wrapper for type safety
// NOTE: There is a variation of the futex syscall with a timeout, which we don't use
static inline long futex(uint32_t *uaddr, int futex_op, uint32_t val, uint32_t val2, uint32_t *uaddr2, uint32_t val3)
{
	return syscall(SYS_futex, uaddr, futex_op, val, val2, uaddr2, val3);
}

static inline long futex_wait(uint32_t *uaddr, uint32_t val)
{
	return futex(uaddr, FUTEX_WAIT, val, 0, NULL, 0);
}

static inline long futex_wake(uint32_t *uaddr, uint32_t waiters_woken)
{
	return futex(uaddr, FUTEX_WAKE, waiters_woken, 0, NULL, 0);
}

static inline void nosv_futex_init(nosv_futex_t *futexvar)
{
	atomic_init(&futexvar->memory, 0);
}

static inline void nosv_futex_destroy(nosv_futex_t *futexvar)
{
	// NO-OP
	assert(futexvar);
}

static inline void nosv_futex_wait(nosv_futex_t *futexvar)
{
	assert(futexvar);

	int val = atomic_fetch_add_explicit(&futexvar->memory, 1, memory_order_release);

	assert(val <= 0);

	if (val == 0) {
		// This wait *may* return EAGAIN, but we don't care
		futex_wait((uint32_t *) &futexvar->memory, 1);
		assert(atomic_load_explicit(&futexvar->memory, memory_order_relaxed) == 0);
	} else {
		assert(val == -1);
	}

	atomic_thread_fence(memory_order_acquire);
}

static inline void nosv_futex_signal(nosv_futex_t *futexvar)
{
	assert(futexvar);
	int val = atomic_fetch_sub_explicit(&futexvar->memory, 1, memory_order_release);

	assert(val == 1 || val == 0);

	if (val > 0) {
		futex_wake((uint32_t *) &futexvar->memory, 1);
	}
}

#endif // FUTEXVAR_H
