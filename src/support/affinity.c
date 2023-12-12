/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <gnu/libc-version.h>

#include "common.h"
#include "instr.h"
#include "config/config.h"
#include "generic/cpuset.h"
#include "generic/hashtable.h"
#include "generic/spinlock.h"
#include "hardware/threads.h"
#include "support/affinity.h"


hash_table_t ht_tid;
hash_table_t ht_pthread;
cpu_set_t original_affinity;
size_t original_affinity_size = sizeof(cpu_set_t);
pthread_once_t once_control = PTHREAD_ONCE_INIT;
thread_local int bypass;

static int (*_next_sched_setaffinity)(pid_t, size_t, const cpu_set_t *);
static int (*_next_sched_getaffinity)(pid_t, size_t, cpu_set_t *);
static int (*_next_pthread_setaffinity_np)(pthread_t, size_t, const cpu_set_t *);
static int (*_next_pthread_getaffinity_np)(pthread_t, size_t, cpu_set_t *);
static int (*_next_pthread_create)(pthread_t *restrict, const pthread_attr_t *restrict, void *(*)(void *), void *restrict );

static nosv_spinlock_t lock = NOSV_SPINLOCK_INITIALIZER;

#define load_next_symbol(sym) __extension__ ({                              \
	char *str;                                                              \
	typeof(&sym) next_sym;                                                  \
	dlerror();                                                              \
	next_sym = (typeof(&sym)) dlsym(RTLD_NEXT, sym);                        \
	str = dlerror();                                                        \
	if (str) {                                                              \
		nosv_abort("%s", str);                                              \
	};                                                                      \
	next_sym;                                                               \
})

#define AUTO_LOAD_SYMBOL(sym) do {                                          \
		assert(!_next_ ## sym);                                             \
		_next_ ## sym = (typeof(&sym)) load_next_symbol( #sym );            \
	} while (0)

static int cmp_lib_ver(const char *version1, const char *version2)
{
    unsigned major1 = 0, minor1 = 0, bugfix1 = 0;
    unsigned major2 = 0, minor2 = 0, bugfix2 = 0;
    sscanf(version1, "%u.%u.%u", &major1, &minor1, &bugfix1);
    sscanf(version2, "%u.%u.%u", &major2, &minor2, &bugfix2);
    if (major1 < major2) return -1;
    if (major1 > major2) return 1;
    if (minor1 < minor2) return -1;
    if (minor1 > minor2) return 1;
    if (bugfix1 < bugfix2) return -1;
    if (bugfix1 > bugfix2) return 1;
    return 0;
}

struct lockup_scope_data {
	const char *libbefore;
	const char *libafter;
	int cnt;
#define LSD_STATUS_NONE 0
#define LSD_STATUS_FOUND 1
#define LSD_STATUS_REVERSED 2
	char status;
};

static int do_check_lockup_scope_order(struct dl_phdr_info *info, size_t size, void *data)
{
	struct lockup_scope_data *lsc = (struct lockup_scope_data *) data;

	// Ignore first entry (the main program)
	if (!lsc->cnt++)
		return 0;

	if (strstr(info->dlpi_name, lsc->libafter)) {
		// We have found "libafter" before "libbefore", abort
		lsc->status = LSD_STATUS_REVERSED;
		return 1;
	} else if (strstr(info->dlpi_name, lsc->libbefore)) {
		// We have found "libbefore" before "libafter", we are done!
		lsc->status = LSD_STATUS_FOUND;
		return 1;
	}

	// Continue searching
	return 0;
}

static void check_lokup_scope_order(void)
{
	Dl_info info;
	const char *glibc_version;
	struct lockup_scope_data lsc = {NULL, NULL, 0, LSD_STATUS_NONE};

	// This function ensures that libnosv.so appears before the libc or
	// libpthread in the shared library loockup scope. This is needed for
	// the interposed nosv symbols to work correctly.

	// First, we need to know the name of the shared library where nosv is
	// found. Usually this will be "libnosv.so", however, it might have
	// another name if nosv was statically linked into another shared library.
	// In this case, we need to find the shared object name.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic" // ignore the warning "ISO C forbids conversion of object pointer to function pointer type"
#pragma GCC diagnostic push
	if (!dladdr((void *)do_check_lockup_scope_order, &info))
		nosv_abort("error in dladdr while trying to find the shared library name where nOS-V is found");
#pragma GCC diagnostic pop
	lsc.libbefore = info.dli_fname;

	// Second, we need to find out this system glibc version. Before glibc
	// 2.34, pthread functions lived in libpthread.so. From this release
	// onwards, pthreads functions were moved into glibc. Depending on the
	// glibc version, we need to check if libpthread appears after libnosv
	// or if glibc appears after libnosv.
	glibc_version = gnu_get_libc_version();
	lsc.libafter = cmp_lib_ver(glibc_version, "2.34") >= 0 ? "/libc.so" : "/libpthread.so";

	// Third, we check the library order by iterating over the lockup scope
	dl_iterate_phdr(do_check_lockup_scope_order, &lsc);

	// Finally, we check the order result
	if (lsc.status == LSD_STATUS_REVERSED) {
		// We found the library containing the system symbols before
		// nosv, abort.
		nosv_abort("nOS-V runtime does not come before %s in initial library list; you should either link the runtime to your application or manually preload it with LD_PRELOAD.", lsc.libafter);
	} else if (lsc.status == LSD_STATUS_NONE) {
		// Neither "libbefore" nor "libafter" were found. If that is an
		// static binary, we should have no conflicts because the libc
		// symbols are weak. Static executables should still return two
		// entries for dl_iterate_phdr one for the main executable and
		// another for linux-vdso.so.
		if (lsc.cnt != 2)
			nosv_abort("Error while searching for %s and %s in the lockup scope order", lsc.libafter, lsc.libbefore);
	} else {
		assert (lsc.status == LSD_STATUS_FOUND);
	}
}

static void affinity_support_init_once(void)
{
	check_lokup_scope_order();
	// Load the next symbol (in library load order) of the following symbols
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic" // ignore the warning "ISO C forbids conversion of object pointer to function pointer type"
#pragma GCC diagnostic push
	AUTO_LOAD_SYMBOL(pthread_create);
	AUTO_LOAD_SYMBOL(sched_setaffinity);
	AUTO_LOAD_SYMBOL(sched_getaffinity);
	AUTO_LOAD_SYMBOL(pthread_setaffinity_np);
	AUTO_LOAD_SYMBOL(pthread_getaffinity_np);
#pragma GCC diagnostic pop
}

void affinity_support_init(void)
{
	int __maybe_unused ret;

	// We need to initialze the affinity support facility as early as
	// possible, because the interposed system calls might be called from
	// library constructors and we need the fallback calls loaded before any
	// of these is called. To do so, symbols can either be loaded at runtime
	// when they are called or here, once we have a chance to initialize
	// this subsystem.

	pthread_once(&once_control, affinity_support_init_once);

	if (!nosv_config.affinity_compat_support)
		return;

	if (bypass_sched_getaffinity(0, original_affinity_size, &original_affinity))
		nosv_abort("cannot read system affinity");

	// Allocate the per-process tid and pid hash tables
	ret = ht_init(&ht_tid, 256, 256);
	assert(!ret);
	ret = ht_init(&ht_pthread, 256, 256);
	assert(!ret);
}

void affinity_support_register_worker(nosv_worker_t *worker, char default_affinity)
{
	assert(worker);

	worker->original_affinity_size = original_affinity_size;
	worker->original_affinity = malloc(worker->original_affinity_size);
	assert(worker->original_affinity);

	if (!nosv_config.affinity_compat_support) {
		bypass_sched_getaffinity(0, worker->original_affinity_size, worker->original_affinity);
		return;
	}

	pthread_t pthread = worker->kthread;
	pid_t tid = worker->tid;

	nosv_spin_lock(&lock);
	ht_insert(&ht_tid, (hash_key_t) tid, worker);
	ht_insert(&ht_pthread, (hash_key_t) pthread, worker);
	if (default_affinity) {
		memcpy(worker->original_affinity, &original_affinity, original_affinity_size);
	} else {
		bypass_sched_getaffinity(0, worker->original_affinity_size, worker->original_affinity);
	}
	nosv_spin_unlock(&lock);
}

static void restore_affinity(nosv_worker_t *worker)
{
	if (CPU_COUNT_S(worker->original_affinity_size, worker->original_affinity) == 1) {
		int cpu = CPU_FIRST_S(worker->original_affinity_size, worker->original_affinity);
		assert(cpu != -1);
		instr_affinity_set(cpu);
	} else {
		instr_affinity_set(-1);
	}
	bypass_sched_setaffinity(0, worker->original_affinity_size, worker->original_affinity);
}

void affinity_support_unregister_worker(nosv_worker_t *worker, char restore)
{
	assert(worker);

	if (!nosv_config.affinity_compat_support) {
		if (restore)
			restore_affinity(worker);
		free(worker->original_affinity);
		return;
	}

	pthread_t pthread = worker->kthread;
	pid_t tid = worker->tid;

	// note, it is essential to remove pthreads from the hash once we are
	// done working with them because the glibc reuses the pthread data
	// structures after join

	nosv_spin_lock(&lock);

	// Restore the worker's affinity to its original value
	// Optionally deactivated with the NOSV_DETACH_NO_RESTORE_AFFINITY flag
	if (restore)
		restore_affinity(worker);

	if (!ht_remove(&ht_tid, (hash_key_t) tid))
		nosv_abort("attempted to remove tid %d from hash list but it does not exist\n", tid);
	if (!ht_remove(&ht_pthread, (hash_key_t) pthread))
		nosv_abort("attempted to remove pthread %lu from hash list but it does not exist\n", pthread);

	nosv_spin_unlock(&lock);

	free(worker->original_affinity);
}

static char pthread_attr_has_cpuset(const pthread_attr_t *attr)
{
	int ret;
	cpu_set_t set;

	// Hacky hack to figure out whether the user ever set a cpuset in the
	// pthread_attr_t or not. If the user did not provide a cpuset, the
	// glibc will fill the provided cpu set with ones, otherwise, it will
	// copy the internal cpuset as long as the provided size is big enough
	// to hold it, otherwise it complains with EINVAL. We pass a cpuset size
	// of 0 to ensure that if the user provided a cpuset it will complain
	// with EINVAL if there is one and that it will succeed otherwise.
	ret = pthread_attr_getaffinity_np(attr, 0, &set);
	if (ret == EINVAL) {
		// The user provided a cpuset, but the size that we have
		// specified (0) is too small to read it
		return 1;
	} else if (ret == 0) {
		// The user did not provide a cpuset, the glibc attempted to
		// fill the set with 1's, but it really did nothing because the
		// size is 0.
		return 0;
	}

	nosv_abort("unexpected pthread_attr_getaffinity_np return value");
}

// The next_* family of function calls should only be called by their
// corresponding interceptor function. These functions call the corresponding
// next symbol in the lookup scope order with respect to the nosv library.

static inline int next_pthread_create(
	pthread_t *restrict thread,
	const pthread_attr_t *restrict attr,
	void *(*start_routine)(void *),
	void *restrict arg
) {
	pthread_once(&once_control, affinity_support_init_once);
	return _next_pthread_create(thread, attr, start_routine, arg);
}

static inline int next_sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask)
{
	pthread_once(&once_control, affinity_support_init_once);
	return _next_sched_setaffinity(pid, cpusetsize, mask);
}

static inline int next_sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask)
{
	pthread_once(&once_control, affinity_support_init_once);
	return _next_sched_getaffinity(pid, cpusetsize, mask);
}

static inline int next_pthread_setaffinity_np(
	pthread_t thread,
	size_t cpusetsize,
	const cpu_set_t *cpuset
) {
	pthread_once(&once_control, affinity_support_init_once);
	return _next_pthread_setaffinity_np(thread, cpusetsize, cpuset);
}

static inline int next_pthread_getaffinity_np(
	pthread_t thread,
	size_t cpusetsize,
	cpu_set_t *cpuset
) {
	pthread_once(&once_control, affinity_support_init_once);
	return _next_pthread_getaffinity_np(thread, cpusetsize, cpuset);
}

// The following pthread_* and sched_* family of interceptor functions are
// intended to be called transparently by user code that makes use of nosv. This
// functions trigger the "fake affinity" mechanism if enabled within the nosv
// config.

int pthread_create(
	pthread_t *restrict thread,
	const pthread_attr_t *restrict attr,
	void *(*start_routine)(void *),
	void *restrict arg
) {
	int ret;
	pthread_attr_t default_attr;
	pthread_attr_t *new_attr_ptr;
	nosv_worker_t *worker = worker_current();
	char needs_reset = 0;

	if (!worker || !nosv_config.affinity_compat_support || bypass)
		return next_pthread_create(thread, attr, start_routine, arg);

	// We need to create the new pthread considering the current worker fake
	// affinity instead of the real one. To do so, we calculate its
	// affinity based on the default provided attr structure, the provided
	// attr in pthread_create and the current worker fake affinity (just
	// like inheritance would do).

	if (attr) {
		if (pthread_attr_has_cpuset(attr)) {
			// The user specified a cpuset explicitly, we respect it
			// just as is
			new_attr_ptr = (pthread_attr_t *) attr;
		} else {
			// The user has provided an attr, but it did not set a
			// cpumask. We set the parent cpuset temporarily and
			// clean it upon return
			new_attr_ptr = (pthread_attr_t *) attr;
			pthread_attr_setaffinity_np(new_attr_ptr, worker->original_affinity_size, worker->original_affinity);
			needs_reset = 1;
		}
	} else {
#ifdef HAVE_pthread_getattr_default_np
		// The user has not provided an attr, let's read the default
		// one. If the user is modifying this object concurrently, it is
		// the users' problem, not ours.
		if (pthread_getattr_default_np(&default_attr))
			nosv_abort("pthread_getattr_default_np");

		if (pthread_attr_has_cpuset(&default_attr)) {
			// There is a cpuset in the default attr, we respect it,
			// and this time we don't need to pass it to
			// pthread_crete, because pthread_create will read it
			// internally
			new_attr_ptr = NULL;
		} else {
			// No cpuset in the default attr, let's modify this copy
			// of the default attr object and set the fake affinity
			// of the parent
			pthread_attr_setaffinity_np(&default_attr, worker->original_affinity_size, worker->original_affinity);
			new_attr_ptr = &default_attr;
		}
#else
		// this system does not implement pthread_getattr_default_np,
		// therefore we create a new attr and set the fake affinity of
		// the parent to simulate fake affinity inheritance
		pthread_attr_init(&default_attr);
		pthread_attr_setaffinity_np(&default_attr, worker->original_affinity_size, worker->original_affinity);
		new_attr_ptr = &default_attr;
#endif
	}

	ret = next_pthread_create(thread, new_attr_ptr, start_routine, arg);

	if (needs_reset) {
		// reset the user attr as it was before calling pthread_create
		cpu_set_t tmp; // needed to silence -Wnonull warning
		assert(new_attr_ptr == attr);
		pthread_attr_setaffinity_np(new_attr_ptr, 0, &tmp);
	}

	return ret;
}

static inline void worker_setaffinity(nosv_worker_t *worker, size_t cpusetsize, const cpu_set_t *mask)
{
	assert(worker->original_affinity);
	if (cpusetsize <= worker->original_affinity_size) {
		memcpy(worker->original_affinity, mask, cpusetsize);
		memset(((char *) worker->original_affinity) + cpusetsize, 0, worker->original_affinity_size - cpusetsize);
	} else {
		free(worker->original_affinity);
		worker->original_affinity = malloc(cpusetsize);
		assert(worker->original_affinity);
		memcpy(worker->original_affinity, mask, cpusetsize);
		worker->original_affinity_size = cpusetsize;
	}
	// we do not really change the thread affinity here, we only
	// keep track of the user affinity that the user set, in case
	// the user requests it with some variant of getaffinity.
}

static inline void worker_getaffinity(nosv_worker_t *worker, size_t cpusetsize, cpu_set_t *mask)
{
	assert(worker->original_affinity);
	CPU_COPY_S(cpusetsize, mask, worker->original_affinity_size, worker->original_affinity);
}

int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask)
{
	int ret;
	nosv_worker_t *worker;

	if (!nosv_config.affinity_compat_support || bypass)
		return next_sched_setaffinity(pid, cpusetsize, mask);

	nosv_spin_lock(&lock);

	if (pid) {
		worker = (nosv_worker_t *) ht_search(&ht_tid, (hash_key_t) pid);
	} else {
		worker = worker_current();
	}

	if (worker) {
		// the thread has been registered as an attached worker
		assert(!pid || worker->tid == pid);
		worker_setaffinity(worker, cpusetsize, mask);
		nosv_spin_unlock(&lock);
		return 0;
	}

	ret = next_sched_setaffinity(pid, cpusetsize, mask);
	nosv_spin_unlock(&lock);
	return ret;
}

int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask)
{
	int ret;
	nosv_worker_t *worker;

	if (!nosv_config.affinity_compat_support || bypass)
		return next_sched_getaffinity(pid, cpusetsize, mask);

	nosv_spin_lock(&lock);

	if (pid) {
		worker = (nosv_worker_t *) ht_search(&ht_tid, (hash_key_t) pid);
	} else {
		worker = worker_current();
	}

	if (worker) {
		// the thread has been registered as an attached worker
		assert(!pid || worker->tid == pid);
		worker_getaffinity(worker, cpusetsize, mask);
		nosv_spin_unlock(&lock);
		return 0;
	}

	ret = next_sched_getaffinity(pid, cpusetsize, mask);
	nosv_spin_unlock(&lock);
	return ret;
}

int pthread_setaffinity_np(
	pthread_t thread,
	size_t cpusetsize,
	const cpu_set_t *cpuset
) {
	int ret;
	nosv_worker_t *worker;

	if (!nosv_config.affinity_compat_support || bypass)
		return next_pthread_setaffinity_np(thread, cpusetsize, cpuset);

	nosv_spin_lock(&lock);

	if (pthread_self() != thread) {
		worker = (nosv_worker_t *) ht_search(&ht_pthread, (hash_key_t) thread);
	} else {
		worker = worker_current();
	}

	if (worker) {
		assert(thread == worker->kthread);
		worker_setaffinity(worker, cpusetsize, cpuset);
		nosv_spin_unlock(&lock);
		return 0;
	}

	ret = next_pthread_setaffinity_np(thread, cpusetsize, cpuset);
	nosv_spin_unlock(&lock);
	return ret;
}

int pthread_getaffinity_np(
	pthread_t thread,
	size_t cpusetsize,
	cpu_set_t *cpuset
) {
	int ret;
	nosv_worker_t *worker;

	if (!nosv_config.affinity_compat_support || bypass)
		return next_pthread_getaffinity_np(thread, cpusetsize, cpuset);

	nosv_spin_lock(&lock);

	if (pthread_self() != thread) {
		worker = (nosv_worker_t *) ht_search(&ht_pthread, (hash_key_t) thread);
	} else {
		worker = worker_current();
	}

	if (worker) {
		assert(thread == worker->kthread);
		worker_getaffinity(worker, cpusetsize, cpuset);
		nosv_spin_unlock(&lock);
		return 0;
	}

	ret = next_pthread_getaffinity_np(thread, cpusetsize, cpuset);
	nosv_spin_unlock(&lock);
	return ret;
}

// The bypass_* family of function wrappers can be called from anywhere within
// the nosv code to invoke the real corresponding function calls without
// triggering the fake affinity mechanism.
//
// These function wrappers call the corresponding original function to ensure
// that the lookup scope order is respected at all times. If we called the
// next_* family of function calls here, we would be skipping other symbol
// interceptors found before the nosv library. For example, it would not call
// libasan interceptors (if available).

int bypass_pthread_create(
	pthread_t *restrict thread,
	const pthread_attr_t *restrict attr,
	void *(*start_routine)(void *),
	void *restrict arg
) {
	bypass++;
	int rc = pthread_create(thread, attr, start_routine, arg);
	bypass--;
	return rc;
}

int bypass_sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask)
{
	bypass++;
	int rc = sched_setaffinity(pid, cpusetsize, mask);
	bypass--;
	return rc;
}

int bypass_sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask)
{
	bypass++;
	int rc = sched_getaffinity(pid, cpusetsize, mask);
	bypass--;
	return rc;
}

int bypass_pthread_setaffinity_np(
	pthread_t thread,
	size_t cpusetsize,
	const cpu_set_t *cpuset
) {
	bypass++;
	int rc = pthread_setaffinity_np(thread, cpusetsize, cpuset);
	bypass--;
	return rc;
}

int bypass_pthread_getaffinity_np(
	pthread_t thread,
	size_t cpusetsize,
	cpu_set_t *cpuset
) {
	bypass++;
	int rc = pthread_getaffinity_np(thread, cpusetsize, cpuset);
	bypass--;
	return rc;
}
