/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>

#include <stdio.h>

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


// Adapted from libasan under llvm compiler-rt/lib/asan/asan_linux.cpp
static int find_first_dso_name(struct dl_phdr_info *info, size_t size, void *data)
{
	const char **name = (const char **) data;

	// Ignore first entry (the main program)
	if (!*name) {
		*name = "";
		return 0;
	}

	// Ignore vDSO. glibc versions earlier than 2.15 (and some patched by
	// distributions) return an empty name for the vDSO entry, so detect
	// this as well
	if (!info->dlpi_name[0] || !strncmp(info->dlpi_name, "linux-", 6))
		return 0;

	// it is ok for libasan to be first on the list
	if (strstr(info->dlpi_name, "libasan.so"))
		return 0;

	// some systems force libsnoop to be loaded by default into all apps
	if (strstr(info->dlpi_name, "libsnoopy.so"))
		return 0;

	*name = info->dlpi_name;
	return 1;
}

static void check_lokup_scope_order(void)
{
	Dl_info info;
	const char *first_dso_name = NULL;

	// This function ensures that libnosv.so appears first in the shared
	// library loockup scope. This is needed for the interposed nosv symbols
	// to work correctly.

	// Find the first valid dso name in the loockup scope
	dl_iterate_phdr(find_first_dso_name, &first_dso_name);

	// If nosv is part of a static binary, we have nothing else to do here.
	// Static executables should still return two entries for dl_iterate_phdr
	// one for the main executable and another for linux-vdso.so. Therefore
	// first_dso_name should be set to "". However, just in case, we check
	// the null pointer case.
	if (!first_dso_name || !first_dso_name[0])
		return;

	// Next, we need to know the name of the shared library where nosv is
	// found. Usually this will be "libnosv.so", however, it might have
	// another name if nosv was statically linked to another shared library.
	// In this case, we need to find the shared object name.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic" // ignore the warning "ISO C forbids conversion of object pointer to function pointer type"
#pragma GCC diagnostic push
	if (!dladdr((void *)find_first_dso_name, &info))
		nosv_abort("error in dladdr while trying to find the shared library name where nOS-V is found");
#pragma GCC diagnostic pop

	if (!strstr(first_dso_name, info.dli_fname))
		nosv_abort("nOS-V runtime does not come first in initial library list; you should either link runtime to your application or manually preload it with LD_PRELOAD. The first dso found is: %s", first_dso_name);
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

	if (!worker || !nosv_config.affinity_compat_support)
		return bypass_pthread_create(thread, attr, start_routine, arg);

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

	ret = bypass_pthread_create(thread, new_attr_ptr, start_routine, arg);

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

	if (!nosv_config.affinity_compat_support)
		goto fallback;

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

fallback:
	ret = bypass_sched_setaffinity(pid, cpusetsize, mask);
	nosv_spin_unlock(&lock);
	return ret;
}

int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask)
{
	int ret;
	nosv_worker_t *worker;

	if (!nosv_config.affinity_compat_support)
		goto fallback;

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

fallback:
	ret = bypass_sched_getaffinity(pid, cpusetsize, mask);
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

	if (!nosv_config.affinity_compat_support)
		goto fallback;

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

fallback:
	ret = bypass_pthread_setaffinity_np(thread, cpusetsize, cpuset);
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

	if (!nosv_config.affinity_compat_support)
		goto fallback;

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

fallback:
	ret = bypass_pthread_getaffinity_np(thread, cpusetsize, cpuset);
	nosv_spin_unlock(&lock);
	return ret;
}

int bypass_pthread_create(
	pthread_t *restrict thread,
	const pthread_attr_t *restrict attr,
	void *(*start_routine)(void *),
	void *restrict arg
) {
	pthread_once(&once_control, affinity_support_init_once);
	return _next_pthread_create(thread, attr, start_routine, arg);
}

int bypass_sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask)
{
	pthread_once(&once_control, affinity_support_init_once);
	return _next_sched_setaffinity(pid, cpusetsize, mask);
}

int bypass_sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask)
{
	pthread_once(&once_control, affinity_support_init_once);
	return _next_sched_getaffinity(pid, cpusetsize, mask);
}

int bypass_pthread_setaffinity_np(
	pthread_t thread,
	size_t cpusetsize,
	const cpu_set_t *cpuset
) {
	pthread_once(&once_control, affinity_support_init_once);
	return _next_pthread_setaffinity_np(thread, cpusetsize, cpuset);
}

int bypass_pthread_getaffinity_np(
	pthread_t thread,
	size_t cpusetsize,
	cpu_set_t *cpuset
) {
	pthread_once(&once_control, affinity_support_init_once);
	return _next_pthread_getaffinity_np(thread, cpusetsize, cpuset);
}
