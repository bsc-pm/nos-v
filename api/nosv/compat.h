/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2025 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_COMPAT_H
#define NOSV_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nosv.h"

#include <errno.h>

/*
	Pthread-compatible nOS-V blocking interface
	These functions aim to be an (almost) drop-in replacement for pthread blocking primitives.
	Note that return codes, instead of nOS-V errors, are POSIX error codes.
*/

#define SIZEOF_NOSV_MUTEX 64

/* Opaque types */
typedef union nosv_mutex_ext {
	uint8_t __pad[SIZEOF_NOSV_MUTEX];
	uint64_t __align;
} nosv_mutex_t;

#define NOSV_MUTEX_INITIALIZER {0}

#define SIZEOF_NOSV_BARRIER 64

typedef union nosv_barrier_ext {
	uint8_t __pad[SIZEOF_NOSV_BARRIER];
	uint64_t __align;
} nosv_barrier_t;

#define SIZEOF_NOSV_COND 64

typedef union nosv_cond_ext {
	uint8_t __pad[SIZEOF_NOSV_COND];
	uint64_t __align;
} nosv_cond_t;

#define NOSV_COND_INITIALIZER {0}

#define SIZEOF_NOSV_RWLOCK 64

typedef union nosv_rwlock_ext {
	uint8_t __pad[SIZEOF_NOSV_RWLOCK];
	uint64_t __align;
} nosv_rwlock_t;

#define NOSV_RWLOCK_INITIALIZER {0}

#define SIZEOF_NOSV_MUTEXATTR 32

typedef union nosv_mutexattr_ext {
	uint8_t __pad[SIZEOF_NOSV_MUTEXATTR];
	uint64_t __align;
} nosv_mutexattr_t;

#define SIZEOF_NOSV_BARRIERATTR 32

typedef union nosv_barrierattr_ext {
	uint8_t __pad[SIZEOF_NOSV_BARRIERATTR];
	uint64_t __align;
} nosv_barrierattr_t;

#define SIZEOF_NOSV_CONDATTR 32

typedef union nosv_condattr_ext {
	uint8_t __pad[SIZEOF_NOSV_CONDATTR];
	uint64_t __align;
} nosv_condattr_t;

#define SIZEOF_NOSV_RWLOCKATTR 32

typedef union nosv_rwlockattr_ext {
	uint8_t __pad[SIZEOF_NOSV_RWLOCKATTR];
	uint64_t __align;
} nosv_rwlockattr_t;

/* Initialize nOS-V mutex attributes */
/* May return:
	- 0 (success)
	- EINVAL (attr is NULL)
*/
int nosv_mutexattr_init(nosv_mutexattr_t *attr);
int nosv_mutexattr_destroy(nosv_mutexattr_t *attr);

/* Initialize a nosv_mutex_t object. The attr object is currently not
 * implemented, use NULL */
/* May return:
	- 0 (success)
	- EINVAL (mutex is NULL)
*/
int nosv_mutex_init(
	nosv_mutex_t *mutex,
	const nosv_mutexattr_t *mutexattr);

/* Destroys a nosv_mutex_t object */
/* May return:
	- 0 (success)
	- EINVAL (mutex is NULL)
*/
int nosv_mutex_destroy(
	nosv_mutex_t *mutex);

/* Similar to pthread_mutex_lock, locks a mutex but calls nosv_pause if the lock
 * is contended */
/* Restriction: Can only be called from a task context */
/* May return:
	- 0 (success)
	- EINVAL (mutex is NULL)
	- EINVAL (the calling task cannot be blocked)
	- ESRCH (called from outside a task context)
*/
int nosv_mutex_lock(
	nosv_mutex_t *mutex);

/* Lock the mutex or return immediately if contended */
/* Restriction: Can only be called from a task context */
/* May return:
	- 0 (success)
	- EINVAL (mutex is NULL)
	- EINVAL (the calling task cannot be blocked)
	- ESRCH (called from outside a task context)
	- EBUSY (the mutex is taken)
*/
int nosv_mutex_trylock(
	nosv_mutex_t *mutex);

/* Unlock a mutex object */
/* Restriction: Can only be called from a task context */
/* May return:
	- 0 (success)
	- EINVAL (mutex is NULL)
	- ESRCH (called from outside a task context)
*/
int nosv_mutex_unlock(
	nosv_mutex_t *mutex);

/* Initialize nOS-V barrier attributes */
/* May return:
	- 0 (success)
	- EINVAL (attr is NULL)
*/
int nosv_barrierattr_init(nosv_barrierattr_t *attr);
int nosv_barrierattr_destroy(nosv_barrierattr_t *attr);

/* Initialize the "barrier" object to wait for "count" threads */
/* May return:
	- 0 (success)
	- EINVAL (barrier is NULL)
	- EINVAL (count is 0)
*/
int nosv_barrier_init(
	nosv_barrier_t *barrier,
	const nosv_barrierattr_t *attr,
	unsigned count);

/* Destroy the "barrier" object. It can be re-initialized afterwards */
/* May return:
	- 0 (success)
	- EINVAL (barrier is NULL)
*/
int nosv_barrier_destroy(
	nosv_barrier_t *barrier);

/* Similar to pthread_barrier_wait, block until "count" threads have reached the
 * barrier */
/* Restriction: Can only be called from a task context */
/* May return:
	- 0 (success)
	- EINVAL (barrier is NULL)
	- ESRCH (called from outside a task context)
*/
int nosv_barrier_wait(
	nosv_barrier_t *barrier);

/* Initialize nOS-V condition variable attributes */
/* May return:
	- 0 (success)
	- EINVAL (attr is NULL)
*/
int nosv_condattr_init(nosv_condattr_t *attr);
int nosv_condattr_destroy(nosv_condattr_t *attr);

/* Initialize a nOS-V condition variable */
/* May return:
	- 0 (success)
	- EINVAL (cond is NULL)
*/
int nosv_cond_init(
	nosv_cond_t *cond,
	const nosv_condattr_t *condattr);

/* Destroy a nOS-V condition variable */
/* May return:
	- 0 (success)
	- EINVAL (cond is NULL)
*/
int nosv_cond_destroy(
	nosv_cond_t *cond);

/* Wake ONE tasks waiting (blocked) on variable. Does nothing if there are no
 * waiters */
/* May return:
	- 0 (success)
	- EINVAL (cond is NULL)
*/
int nosv_cond_signal(nosv_cond_t *cond);

/* Wake ALL tasks waiting (blocked) on variable. Does nothing if there are no
 * waiters */
/* May return:
	- 0 (success)
	- EINVAL (cond is NULL)
*/
int nosv_cond_broadcast(nosv_cond_t *cond);

/* Similar to pthread_cond_wait, block until signaled */
/* Restriction: Can only be called with mutex locked */
/* May return:
	- 0 (success)
	- EINVAL (cond or mutex is NULL)
	- EINVAL (the calling task cannot be blocked)
	- ESRCH (called from outside a task context)
*/
int nosv_cond_wait(
	nosv_cond_t *cond,
	nosv_mutex_t *mutex);

/* Similar to pthread_cond_timedwait, block until signaled or the deadline
 * expires */
/* Restriction: Can only be called with mutex locked */
/* May return:
	- 0 (success)
	- EINVAL (cond or mutex is NULL)
	- EINVAL (the calling task cannot be blocked)
	- ESRCH (called from outside a task context)
*/
int nosv_cond_timedwait(
	nosv_cond_t *cond,
	nosv_mutex_t *mutex,
	const struct timespec *abstime);

/* Same as the above nosv_cond_wait but using a pthread mutex internally */
/* May return:
	- 0 (success)
	- EINVAL (cond or mutex is NULL)
	- EINVAL (the calling task cannot be blocked)
	- ESRCH (called from outside a task context)
*/
int nosv_cond_wait_pthread(
	nosv_cond_t *cond,
	pthread_mutex_t *mutex);

/* Same as the above nosv_cond_timedwait but using a pthread mutex internally */
/* May return:
	- 0 (success)
	- EINVAL (cond or mutex is NULL)
	- EINVAL (the calling task cannot be blocked)
	- ESRCH (called from outside a task context)
*/
int nosv_cond_timedwait_pthread(
	nosv_cond_t *cond,
	pthread_mutex_t *mutex,
	const struct timespec *abstime);

/* Initialize nOS-V read-write lock attributes */
/* May return:
	- 0 (success)
	- EINVAL (attr is NULL)
*/
int nosv_rwlockattr_init(nosv_rwlockattr_t *attr);
int nosv_rwlockattr_destroy(nosv_rwlockattr_t *attr);

/* Initialize a nosv_rwlock_t object. The attr object is currently not
 * implemented, use NULL */
/* May return:
	- 0 (success)
	- EINVAL (rwlock is NULL)
*/
int nosv_rwlock_init(
	nosv_rwlock_t *rwlock,
	const nosv_rwlockattr_t *attr);

/* Destroys a nosv_rwlock_t object */
/* May return:
	- 0 (success)
	- EINVAL (rwlock is NULL)
*/
int nosv_rwlock_destroy(
	nosv_rwlock_t *rwlock);

/* Similar to pthread_rwlock_wrlock, locks a rwlock but calls nosv_pause if the lock
 * is contended */
/* Restriction: Can only be called from a task context */
/* May return:
	- 0 (success)
	- EINVAL (rwlock is NULL)
	- EINVAL (the calling task cannot be blocked)
	- ESRCH (called from outside a task context)
*/
int nosv_rwlock_wrlock(
	nosv_rwlock_t *rwlock);

/* Similar to pthread_rwlock_rdlock, locks a rwlock but calls nosv_pause if the lock
 * is contended */
/* Restriction: Can only be called from a task context */
/* May return:
	- 0 (success)
	- EINVAL (rwlock is NULL)
	- EINVAL (the calling task cannot be blocked)
	- ESRCH (called from outside a task context)
*/
int nosv_rwlock_rdlock(
	nosv_rwlock_t *rwlock);

/* Lock the rwlock or return immediately if contended */
/* Restriction: Can only be called from a task context */
/* May return:
	- 0 (success)
	- EINVAL (rwlock is NULL)
	- ESRCH (called from outside a task context)
	- EBUSY (the lock is contended)
*/
int nosv_rwlock_trywrlock(
	nosv_rwlock_t *rwlock);

/* Lock the rwlock or return immediately if contended */
/* Restriction: Can only be called from a task context */
/* May return:
	- 0 (success)
	- EINVAL (rwlock is NULL)
	- ESRCH (called from outside a task context)
	- EBUSY (the lock is contended)
*/
int nosv_rwlock_tryrdlock(
	nosv_rwlock_t *rwlock);

/* Unlock a rwlock object */
/* Restriction: Can only be called from a task context */
/* May return:
	- 0 (success)
	- EINVAL (rwlock is NULL)
	- ESRCH (called from outside a task context)
*/
int nosv_rwlock_unlock(
	nosv_rwlock_t *rwlock);

#ifdef __cplusplus
}
#endif

#endif // NOSV_COMPAT_H
