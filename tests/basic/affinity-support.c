/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023-2024 Barcelona Supercomputing Center (BSC)
*/

#include <nosv.h>
#include <string.h>
#include <sched.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "test.h"


#ifndef gettid
#define gettid() syscall(SYS_gettid)
#endif

#ifndef getcpu
#if defined(SYS_getcpu)
#define getcpu(cpu, node) syscall(SYS_getcpu, cpu, node)
#define HAVE_getcpu
#endif
#else
#define HAVE_getcpu
#endif

typedef enum rat_cmd {
	RAT_NONE,
	RAT_DONE,
	RAT_READY,
	RAT_END,
	RAT_ATTACH,
	RAT_DETACH,
	RAT_CHECK_AFFINITY,
	RAT_FAILURE
} rat_cmd_t;

typedef struct parg {
	_Atomic (cpu_set_t *)set;
	_Atomic nosv_task_t task;
	char *msg;
	atomic_int status;
	_Atomic pid_t tid;
	char attached;
} parg_t;

test_t test;


static void pr_cpuset(const char *msg, size_t cpusetsize, const cpu_set_t *cpuset)
{
	#define BUFFER_SIZE (1024)
	char buf[BUFFER_SIZE];
	int count = CPU_COUNT_S(cpusetsize, cpuset);
	int i = 0;
	int found = 0;
	int written = 0;
	int start = -1;
	int end;

	if (!cpuset) {
		fprintf(stderr, "%s: null\n", msg);
		return;
	}

	if (count == 8 * cpusetsize) {
		fprintf(stderr, "%s: all set\n", msg);
		return;
	}

	do {
		int isset = CPU_ISSET_S(i, cpusetsize, cpuset);

		if (isset) {
			if (start == -1)
				start = i;
			found++;
		} else {
			if (start != -1) {
				end = i - 1;
				if (start == end) {
					written += snprintf(buf + written,
							    BUFFER_SIZE - written,
							    "%d,", i);
				} else {
					written += snprintf(buf + written,
							    BUFFER_SIZE - written,
							    "%d-%d,", start, end);
				}
				start = -1;
			}
		}

		i++;
	} while ((count != found) && (written < BUFFER_SIZE) && (i < (8 * cpusetsize)));

	if ((start != -1) && (written < BUFFER_SIZE)) {
		end = i - 1;
		if (start == end) {
			written += snprintf(buf + written,
					    BUFFER_SIZE - written,
					    "%d,", i);
		} else {
			written += snprintf(buf + written,
					    BUFFER_SIZE - written,
					    "%d-%d,", start, end);
		}
	}

	// write an "X" to indicate that some data migth be lost
	if (written == BUFFER_SIZE)
		buf[written-2] = 'X';
	else if (written)
		buf[written-1] = '\0';
	else
		buf[0] = '\0';

	if (count != found) {
		fprintf(stderr, "pr_cpuset: cpucount returned %d but we could only find %d\n",
			 count, found);
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "%s: %s\n", msg, buf);
}

#define ratcase(val) \
	case val: \
		ret = # val "\n"; \
		break;
char *ratstr(rat_cmd_t cmd)
{
	char *ret;
	switch (cmd) {
	ratcase(RAT_NONE)
	ratcase(RAT_DONE)
	ratcase(RAT_READY)
	ratcase(RAT_END)
	ratcase(RAT_ATTACH)
	ratcase(RAT_DETACH)
	ratcase(RAT_CHECK_AFFINITY)
	ratcase(RAT_FAILURE)
	default:
		fprintf(stderr, "rat value not valid\n");
		exit(EXIT_FAILURE);
	}
	return ret;
}

int rat_wait_reply(parg_t *parg, rat_cmd_t reply)
{
	rat_cmd_t status;
	while (((status = atomic_load_explicit(&parg->status, memory_order_acquire)) != RAT_FAILURE)
	       && status != reply)
		nosv_yield(NOSV_YIELD_NONE);
	return status == RAT_FAILURE;
}

void rat_send_command(parg_t *parg, rat_cmd_t cmd, char attached)
{
	atomic_store_explicit(&parg->status, cmd, memory_order_release);
	if (attached) {
		//fprintf(stderr, "nosv_submit\n");
		nosv_submit(parg->task, NOSV_SUBMIT_UNLOCKED);
	}
}

int rat_cmd(parg_t *parg, rat_cmd_t cmd, char attached)
{
	int ret;
	//fprintf(stderr, "rat: sending cmd %s\n", ratstr(cmd));
	rat_send_command(parg, cmd, attached);
	rat_cmd_t reply = (cmd == RAT_END)? RAT_DONE : RAT_READY;
	ret = rat_wait_reply(parg, reply);
	//fprintf(stderr, "rat: received cmd %s\n", ratstr(reply));
	return ret;
}

void *getaffinity_test(void *arg)
{
	int rc;
	parg_t *parg = (parg_t *) arg;
	cpu_set_t new;
	nosv_task_t task;
	pthread_t self = pthread_self();
	cpu_set_t *set = atomic_load_explicit(&parg->set, memory_order_relaxed);
	size_t cpusetsize = sizeof(cpu_set_t);

	// The following sched_getaffinity bypasses the nosv affinity support
	// because this thread has not been attached yet. It will return the
	// real system affinity, which should be the same as the original one,
	// even in the case that this thread has been created from an attached
	// thread.
	rc = sched_getaffinity(0, cpusetsize, &new);
	test_check(&test, !rc && CPU_EQUAL(set, &new), "%s: sched_getaffinity before attach on a pthread returns the original parent affinity", parg->msg);
	rc = pthread_getaffinity_np(self, cpusetsize, &new);
	test_check(&test, !rc && CPU_EQUAL(set, &new), "%s: pthread_getaffinity_np before attach on a pthread returns the original parent affinity", parg->msg);

	nosv_attach(&task, NULL, "gettafinity_test", NOSV_ATTACH_NONE);

	// After attaching, getaffinity should return still the original
	// affinity
	rc = sched_getaffinity(0, cpusetsize, &new);
	test_check(&test, !rc && CPU_EQUAL(set, &new), "%s: sched_getaffinity after attach on a pthread returns the original parent affinity", parg->msg);
	rc = pthread_getaffinity_np(self, cpusetsize, &new);
	test_check(&test, !rc && CPU_EQUAL(set, &new), "%s: phtread_getaffinity_np after attach on a pthread returns the original parent affinity", parg->msg);

	nosv_detach(NOSV_DETACH_NONE);

	// unblock the parent
	if (parg->attached)
		nosv_submit(parg->task, NOSV_SUBMIT_UNLOCKED);

	return NULL;
}

static void run_thread(void *(*func)(void *), pthread_attr_t *attr, parg_t *parg)
{
	int rc;
	pthread_t pthread;

	if ((rc = pthread_create(&pthread, attr, func, parg))) {
		fprintf(stderr, "error: pthread_create: %s\n", strerror(rc));
		exit(EXIT_FAILURE);
	}

	//fprintf(stderr, "run_thread: pthread_create %lu\n", pthread);

	if (parg->attached)
		nosv_pause(NOSV_PAUSE_NONE);

	if ((rc = pthread_join(pthread, NULL))) {
		fprintf(stderr, "error: pthread_join: %s\n", strerror(rc));
		exit(EXIT_FAILURE);
	}
}

void *remote_affinity_test(void *arg)
{
	int rc;
	rat_cmd_t status;
	nosv_task_t task;
	cpu_set_t new;
	cpu_set_t *set;
	parg_t *parg = (parg_t *) arg;
	pthread_t self = pthread_self();
	int attached = 0;

	atomic_store_explicit(&parg->tid, gettid(), memory_order_relaxed);
	atomic_store_explicit(&parg->status, RAT_READY, memory_order_release);

	while (RAT_END != (status = atomic_load_explicit(&parg->status, memory_order_acquire)))
	{
		//fprintf(stderr, "rat current status: %s\n", ratstr(status));
		switch (status) {
		case RAT_ATTACH:
			nosv_attach(&task, NULL, "remote_affinity_test", NOSV_ATTACH_NONE);
			attached = 1;
			atomic_store_explicit(&parg->task, task, memory_order_relaxed);
			atomic_store_explicit(&parg->status, RAT_READY, memory_order_release);
			break;
		case RAT_DETACH:
			nosv_detach(NOSV_DETACH_NONE);
			attached = 0;
			atomic_store_explicit(&parg->status, RAT_READY, memory_order_release);
			break;
		case RAT_CHECK_AFFINITY:
			set = atomic_load_explicit(&parg->set, memory_order_relaxed);
			rc = sched_getaffinity(0, sizeof(cpu_set_t), &new);
			test_check(&test, !rc && CPU_EQUAL(set, &new), "%s: sched_getaffinity", parg->msg);
			rc = pthread_getaffinity_np(self, sizeof(cpu_set_t), &new);
			test_check(&test, !rc && CPU_EQUAL(set, &new), "%s: phtread_getaffinity_np", parg->msg);
			atomic_store_explicit(&parg->status, RAT_READY, memory_order_release);
			break;
		}

		if (attached) {
			rc = nosv_pause(NOSV_PAUSE_NONE);
			assert(!rc);
		}
	}

	atomic_store_explicit(&parg->status, RAT_DONE, memory_order_release);

	return NULL;
}

static void run_basic_rat_test(cpu_set_t *original, cpu_set_t *target)
{
	// number of tests: 4 + 3*2 = 10
	int rc;
	pthread_t pthread;
	cpu_set_t set;
	parg_t parg;
	size_t cpusetsize = sizeof(cpu_set_t);
	pid_t tid;

	atomic_init(&parg.status, RAT_NONE);

	if ((rc = pthread_create(&pthread, NULL, remote_affinity_test, &parg))) {
		fprintf(stderr, "error: pthread_create: %s\n", strerror(rc));
		exit(EXIT_FAILURE);
	}

	//fprintf(stderr, "rat: pthread_create %lu\n", pthread);

	// wait for the rat to be created
	rat_wait_reply(&parg, RAT_READY);
	tid = (pid_t) atomic_load_explicit(&parg.tid, memory_order_relaxed);

	// get the remote affinity before attach
	rc = pthread_getaffinity_np(pthread, cpusetsize, &set);
	test_check(&test, !rc && CPU_EQUAL(&set, original), "rat: pthread_getaffinity_np on remote thread before attach returns the original parent affinity");
	rc = sched_getaffinity(tid, cpusetsize, &set);
	test_check(&test, !rc && CPU_EQUAL(&set, original), "rat: sched_getaffinity on remote thread before attach returns the original parent affinity");

	// set a remote affinity before attach
	parg.msg = "rat: pthread_setaffinity_np before attach";
	atomic_store_explicit(&parg.set, target, memory_order_relaxed);
	rc = pthread_setaffinity_np(pthread, cpusetsize, target);
	assert(!rc);
	rat_cmd(&parg, RAT_CHECK_AFFINITY, 0);

	parg.msg = "rat: sched_setaffinity_np before attach";
	atomic_store_explicit(&parg.set, original, memory_order_relaxed);
	rc = sched_setaffinity(tid, cpusetsize, original);
	assert(!rc);
	rat_cmd(&parg, RAT_CHECK_AFFINITY, 0);

	// attach rat
	rat_cmd(&parg, RAT_ATTACH, 0);

	// get the remote affinity after attach
	rc = pthread_getaffinity_np(pthread, cpusetsize, &set);
	test_check(&test, !rc && CPU_EQUAL(&set, original), "rat: pthread_getaffinity_np on remote thread after attach returns the original parent affinity");
	rc = sched_getaffinity(tid, cpusetsize, &set);
	test_check(&test, !rc && CPU_EQUAL(&set, original), "rat: sched_getaffinity on remote thread after attach returns the original parent affinity");

	// set a remote affinity after attach
	parg.msg = "rat: pthread_setaffinity_np after attach";
	atomic_store_explicit(&parg.set, target, memory_order_relaxed);
	rc = pthread_setaffinity_np(pthread, cpusetsize, target);
	assert(!rc);
	rat_cmd(&parg, RAT_CHECK_AFFINITY, 1);

	// detach the rat
	rat_cmd(&parg, RAT_DETACH, 1);

	// kill the rat
	rat_cmd(&parg, RAT_END, 0);

	if ((rc = pthread_join(pthread, NULL))) {
		fprintf(stderr, "error: pthread_join: %s\n", strerror(rc));
		exit(EXIT_FAILURE);
	}
}

void *looping_remote_affinity_test(void *arg)
{
	int rc;
	rat_cmd_t status;
	nosv_task_t task;
	cpu_set_t set;
	parg_t *parg = (parg_t *) arg;
	pthread_t self = pthread_self();
	cpu_set_t *expected_set;
	int attached = 0;
	char pass;

	atomic_store_explicit(&parg->tid, gettid(), memory_order_relaxed);
	atomic_store_explicit(&parg->status, RAT_READY, memory_order_release);

	while (RAT_END != (status = atomic_load_explicit(&parg->status, memory_order_acquire)))
	{
		// This rat (remote affinity test) attaches and detaches in a
		// loop. At any point, its parent might change the rat's
		// affinity and ask the rat to check whether its affinity makes
		// sense. We can't know in advance at which point the rat will
		// receive the request, the point is to test whether concurrent
		// remote setaffinities and attach/detach are safe.

		if (!attached) {
			nosv_attach(&task, NULL, "looping_remote_affinty_test", NOSV_ATTACH_NONE);
			atomic_store_explicit(&parg->task, task, memory_order_relaxed);
			attached = 1;
		} else {
			nosv_detach(NOSV_DETACH_NONE);
			atomic_store_explicit(&parg->task, NULL, memory_order_relaxed);
			attached = 0;
		}

		switch (status) {
		case RAT_CHECK_AFFINITY:
			pass = 1;
			expected_set = atomic_load_explicit(&parg->set, memory_order_relaxed);
			rc = sched_getaffinity(0, sizeof(cpu_set_t), &set);
			if (rc || !CPU_EQUAL(expected_set, &set)) {
				pass = 0;
				fprintf(stderr, "looping rat: sched_getaffinity: missmatch:\n");
				pr_cpuset(" - expected", sizeof(cpu_set_t), expected_set);
				pr_cpuset(" - found   ", sizeof(cpu_set_t), &set);
			}
			rc = pthread_getaffinity_np(self, sizeof(cpu_set_t), &set);
			if (rc || !CPU_EQUAL(expected_set, &set)) {
				pass = 0;
				fprintf(stderr, "looping rat: pthread_getaffinity_np: missmatch:\n");
				pr_cpuset(" - expected", sizeof(cpu_set_t), expected_set);
				pr_cpuset(" - found   ", sizeof(cpu_set_t), &set);
			}

			if (!pass) {
				atomic_store_explicit(&parg->status, RAT_FAILURE, memory_order_release);
				return NULL;
			}
			atomic_store_explicit(&parg->status, RAT_READY, memory_order_release);
			break;
		}
	}

	if (attached)
		nosv_detach(NOSV_DETACH_NONE);

	atomic_store_explicit(&parg->status, RAT_DONE, memory_order_release);

	return NULL;
}

static void run_looping_rat_test(cpu_set_t *original, cpu_set_t *target)
{
	int rc;
	pthread_t pthread;
	cpu_set_t set;
	parg_t parg;
	size_t cpusetsize = sizeof(cpu_set_t);
	pid_t tid;
	char pass = 1;

	atomic_init(&parg.status, RAT_NONE);

	if ((rc = pthread_create(&pthread, NULL, looping_remote_affinity_test, &parg))) {
		fprintf(stderr, "error: pthread_create: %s\n", strerror(rc));
		exit(EXIT_FAILURE);
	}

	// wait for the rat to be created
	rat_wait_reply(&parg, RAT_READY);
	tid = (pid_t) atomic_load_explicit(&parg.tid, memory_order_relaxed);

	for (int i = 0; (i < 1000) && pass; i++) {
		if (i%2) {
			parg.msg = "looping rat: sched_setaffinity";
			atomic_store_explicit(&parg.set, original, memory_order_relaxed);
			rc = sched_setaffinity(tid, cpusetsize, original);
			assert(!rc);
		} else {
			parg.msg = "looping rat: pthread_setaffinity_np";
			atomic_store_explicit(&parg.set, target, memory_order_relaxed);
			rc = pthread_setaffinity_np(pthread, cpusetsize, target);
			assert(!rc);
		}
		pass = !rat_cmd(&parg, RAT_CHECK_AFFINITY, 0);
	}

	test_check(&test, pass, "rat: setaffinity on concurrent attach/detach yields expected results");

	if (pass)
		// kill the rat
		rat_cmd(&parg, RAT_END, 0);

	if ((rc = pthread_join(pthread, NULL))) {
		fprintf(stderr, "error: pthread_join: %s\n", strerror(rc));
		exit(EXIT_FAILURE);
	}
}

void run_getaffiniy_before_attach_test(cpu_set_t *original)
{
	parg_t parg;
	pthread_attr_t attr;

	parg.attached = 0;

	parg.msg = "non-attached parent";
	atomic_store_explicit(&parg.set, original, memory_order_relaxed);
	run_thread(getaffinity_test, NULL, &parg);
}

void run_getaffinity_after_attach_test(cpu_set_t *original, cpu_set_t *target, nosv_task_t task)
{
	int rc;
	parg_t parg;
	pthread_attr_t attr;
	cpu_set_t new;
	cpu_set_t testset;
	int cpu, cpu_attached;
	int count = CPU_COUNT(original);
	pthread_t self = pthread_self();

	parg.task = task;
	parg.attached = 1;

	// check basic sched and pthread getaffinity
	sched_getaffinity(0, sizeof(cpu_set_t), &new);
	test_check(&test, CPU_EQUAL(original, &new), "sched_getaffinity returns the affinity before nosv_attach");
	pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &new);
	test_check(&test, CPU_EQUAL(original, &new), "pthread_getaffinity_np returns the affinity before nosv_attach");

	// create pthread without attr from a nosv_attached thread, the new
	// thread should inherit the fake affinity
	parg.msg = "no attr";
	atomic_store_explicit(&parg.set, original, memory_order_relaxed);
	run_thread(getaffinity_test, NULL, &parg);

	// repeat, but using an attr without cpuset
	parg.msg = "attr without cpuset";
	pthread_attr_init(&attr);
	run_thread(getaffinity_test, &attr, &parg);

	// repeat, but using an attr with a cpuset
	parg.msg = "attr with cpuset";
	pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), target);
	atomic_store_explicit(&parg.set, target, memory_order_relaxed);
	run_thread(getaffinity_test, &attr, &parg);
	pthread_attr_destroy(&attr);

#ifdef HAVE_pthread_getattr_default_np
	// repeat, but using a default attr without cpuset. This is the same as
	// the first test, but we do it explicitly just in case.
	parg.msg = "default attr without cpuset";
	pthread_attr_init(&attr);
	pthread_setattr_default_np(&attr);
	atomic_store_explicit(&parg.set, original, memory_order_relaxed);
	run_thread(getaffinity_test, NULL, &parg);
	pthread_attr_destroy(&attr);

	// repeat, but using a default attr with cpuset
	parg.msg = "default attr with cpuset";
	pthread_attr_init(&attr);
	pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), target);
	pthread_setattr_default_np(&attr);
	atomic_store_explicit(&parg.set, target, memory_order_relaxed);
	run_thread(getaffinity_test, NULL, &parg);
	pthread_attr_destroy(&attr);

	// reset default attr
	pthread_attr_init(&attr);
	pthread_setattr_default_np(&attr);
#endif

	// check basic sched_setaffinity
	char pass = 1;
	for (int i = 0, j = 0; j < count; i++) {
		if (!CPU_ISSET(i, original))
			continue;

		CPU_ZERO(&testset);
		CPU_SET(i, &testset);

		sched_setaffinity(0, sizeof(cpu_set_t), &testset);
		sched_getaffinity(0, sizeof(cpu_set_t), &new);
		if (!CPU_EQUAL(&testset, &new)) {
			pass = 0;
			break;
		}
		pthread_setaffinity_np(self, sizeof(cpu_set_t), &testset);
		pthread_getaffinity_np(self, sizeof(cpu_set_t), &new);
		if (!CPU_EQUAL(&testset, &new)) {
			pass = 0;
			break;
		}
		j++;
	}
	test_check(&test, pass, "sched/pthread_getaffinity returns the affinity set by sched_setaffinity");

#ifdef HAVE_getcpu
	if (getcpu(&cpu_attached, NULL)) {
		perror("getcpu");
		exit(EXIT_FAILURE);
	}

	pass = 1;
	for (int i = 0; i < count; i++) {
		if (!CPU_ISSET(i, original))
			continue;

		CPU_ZERO(&testset);
		CPU_SET(i, &testset);

		sched_setaffinity(0, sizeof(cpu_set_t), &testset);

		if (getcpu(&cpu, NULL)) {
			perror("getcpu");
			exit(EXIT_FAILURE);
		}
		if (cpu_attached != cpu) {
			pass = 0;
			break;
		}

		pthread_setaffinity_np(self, sizeof(cpu_set_t), &testset);

		if (getcpu(&cpu, NULL)) {
			perror("getcpu");
			exit(EXIT_FAILURE);
		}
		if (cpu_attached != cpu) {
			pass = 0;
			break;
		}
	}
	test_check(&test, pass, "sched/pthread_setaffinity do not really migrate the current thread");
#endif

	// restore the original affinity
	rc = sched_setaffinity(0, sizeof(cpu_set_t), original);
	assert(!rc);
}

int main() {
	cpu_set_t original, target;
	nosv_task_t task;
	int ntests = 39;

	// skip unsupported functions associated tests
#ifndef HAVE_pthread_getattr_default_np
	ntests -= 4 * 2;
#endif

#ifndef HAVE_getcpu
	ntests -= 1;
#endif

	// initialize test data
	test_init(&test, ntests);
	sched_getaffinity(0, sizeof(cpu_set_t), &original);
	memcpy(&target, &original, sizeof(cpu_set_t));
	if (CPU_COUNT(&original) > 1) {
		// In order to test some variability, create a cpuset equal to
		// the original but with a cpu less
		for (int i = 0;; i++) {
			if (!CPU_ISSET(i, &target))
				continue;

			CPU_CLR(i, &target);
			break;
		}
	}

	nosv_init();

	// run tests before attaching main thread
	run_getaffiniy_before_attach_test(&original);

	// attach main thread
	nosv_attach(&task, NULL, "main", NOSV_ATTACH_NONE);

	// run tests after attaching main thread
	run_getaffinity_after_attach_test(&original, &target, task);

	// run remote affinity tests
	run_basic_rat_test(&original, &target);

	// run remote affinity test
	run_looping_rat_test(&original, &target);

	// TODO add parallel rat test

	// finalize test
	nosv_detach(NOSV_DETACH_NONE);
	nosv_shutdown();
}
