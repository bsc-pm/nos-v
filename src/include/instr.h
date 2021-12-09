/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef INSTR_H
#define INSTR_H

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef ENABLE_INSTRUMENTATION
#include <ovni.h>
#endif

#include "common.h"
#include "compat.h"
#include "hardware/pids.h"
#include "hardware/threads.h"
#include "nosv-internal.h"

#ifdef ENABLE_INSTRUMENTATION
#define INSTR_0ARG(name, mcv)              \
	static inline void name(void)      \
	{                                  \
		struct ovni_ev ev = {0};   \
		ovni_ev_set_clock(&ev, ovni_clock_now()); \
		ovni_ev_set_mcv(&ev, mcv); \
		ovni_ev_emit(&ev);              \
	}

#define INSTR_1ARG(name, mcv, ta, a)                             \
	static inline void name(ta a)                            \
	{                                                        \
		struct ovni_ev ev = {0};                         \
		ovni_ev_set_clock(&ev, ovni_clock_now());        \
		ovni_ev_set_mcv(&ev, mcv);                       \
		ovni_payload_add(&ev, (uint8_t *)&a, sizeof(a)); \
		ovni_ev_emit(&ev);                                    \
	}

#define INSTR_2ARG(name, mcv, ta, a, tb, b)                      \
	static inline void name(ta a, tb b)                      \
	{                                                        \
		struct ovni_ev ev = {0};                         \
		ovni_ev_set_clock(&ev, ovni_clock_now());        \
		ovni_ev_set_mcv(&ev, mcv);                       \
		ovni_payload_add(&ev, (uint8_t *)&a, sizeof(a)); \
		ovni_payload_add(&ev, (uint8_t *)&b, sizeof(b)); \
		ovni_ev_emit(&ev);                                    \
	}

#define INSTR_3ARG(name, mcv, ta, a, tb, b, tc, c)               \
	static inline void name(ta a, tb b, tc c)                \
	{                                                        \
		struct ovni_ev ev = {0};                         \
		ovni_ev_set_clock(&ev, ovni_clock_now());        \
		ovni_ev_set_mcv(&ev, mcv);                       \
		ovni_payload_add(&ev, (uint8_t *)&a, sizeof(a)); \
		ovni_payload_add(&ev, (uint8_t *)&b, sizeof(b)); \
		ovni_payload_add(&ev, (uint8_t *)&c, sizeof(c)); \
		ovni_ev_emit(&ev);                                    \
	}
#else // ENABLE_INSTRUMENTATION
#define INSTR_0ARG(name, mcv)         \
	static inline void name(void) \
	{                             \
	}

#define INSTR_1ARG(name, mcv, ta, a)     \
	static inline void name(ta a)    \
	{                                \
	}

#define INSTR_2ARG(name, mcv, ta, a, tb, b)     \
	static inline void name(ta a, tb b)     \
	{                                       \
	}

#define INSTR_3ARG(name, mcv, ta, a, tb, b, tc, c)     \
	static inline void name(ta a, tb b, tc c)      \
	{                                              \
	}
#endif // ENABLE_INSTRUMENTATION

// ----------------------- nOS-V events  ---------------------------

INSTR_0ARG(instr_worker_enter, "VHw")
INSTR_0ARG(instr_worker_exit, "VHW")
INSTR_0ARG(instr_delegate_enter, "VHd")
INSTR_0ARG(instr_delegate_exit, "VHD")

INSTR_0ARG(instr_sched_recv, "VSr")
INSTR_0ARG(instr_sched_send, "VSs")
INSTR_0ARG(instr_sched_self_assign, "VS@")
INSTR_0ARG(instr_sched_hungry, "VSh")
INSTR_0ARG(instr_sched_fill, "VSf")
INSTR_0ARG(instr_sched_server_enter, "VS[")
INSTR_0ARG(instr_sched_server_exit, "VS]")

INSTR_0ARG(instr_sched_submit_enter, "VU[")
INSTR_0ARG(instr_sched_submit_exit, "VU]")

INSTR_0ARG(instr_salloc_enter, "VMa")
INSTR_0ARG(instr_salloc_exit, "VMA")
INSTR_0ARG(instr_sfree_enter, "VMf")
INSTR_0ARG(instr_sfree_exit, "VMF")

INSTR_0ARG(instr_submit_enter, "VAs")
INSTR_0ARG(instr_submit_exit, "VAS")
INSTR_0ARG(instr_pause_enter, "VAp")
INSTR_0ARG(instr_pause_exit, "VAP")
INSTR_0ARG(instr_yield_enter, "VAy")
INSTR_0ARG(instr_yield_exit, "VAY")
INSTR_0ARG(instr_waitfor_enter, "VAw")
INSTR_0ARG(instr_waitfor_exit, "VAW")
INSTR_0ARG(instr_schedpoint_enter, "VAc")
INSTR_0ARG(instr_schedpoint_exit, "VAC")

INSTR_2ARG(instr_task_create, "VTc", uint32_t, task_id, uint32_t, type_id)
INSTR_1ARG(instr_task_execute, "VTx", uint32_t, task_id)
INSTR_1ARG(instr_task_pause, "VTp", uint32_t, task_id)
INSTR_1ARG(instr_task_resume, "VTr", uint32_t, task_id)
INSTR_1ARG(instr_task_end, "VTe", uint32_t, task_id)

#ifdef ENABLE_INSTRUMENTATION

// A jumbo event is needed to encode a large label
static inline void instr_type_create(uint32_t typeid, const char *label)
{
	size_t bufsize, label_len, size_left;
	uint8_t buf[1024], *p;

	p = buf;
	size_left = sizeof(buf);
	bufsize = 0;

	memcpy(p, &typeid, sizeof(typeid));
	p += sizeof(typeid);
	size_left -= sizeof(typeid);
	bufsize += sizeof(typeid);

	if (label == NULL)
		label = "";

	label_len = strlen(label);

	// Truncate the label if required
	if (label_len > size_left - 1) {
		// Maximum length of the label without the '\0'
		label_len = size_left - 1;

		// FIXME: Print detailed truncation message
		nosv_warn("The task label is too large, truncated\n");
	}

	memcpy(p, label, label_len);
	p += label_len;
	bufsize += label_len;

	// Always terminate the label
	*p = '\0';
	bufsize += 1;

	struct ovni_ev ev = {0};
	ovni_ev_set_clock(&ev, ovni_clock_now());
	ovni_ev_set_mcv(&ev, "VYc");
	ovni_ev_jumbo_emit(&ev, buf, bufsize);
}

#else // ENABLE_INSTRUMENTATION

static inline void instr_type_create(uint32_t typeid, const char *label)
{
}

#endif // ENABLE_INSTRUMENTATION

// ----------------------- Ovni events  ---------------------------

INSTR_0ARG(instr_burst, "OB.")

INSTR_1ARG(instr_affinity_set, "OAs", int32_t, cpu)
INSTR_2ARG(instr_affinity_remote, "OAr", int32_t, cpu, int32_t, tid)

INSTR_2ARG(instr_cpu_count, "OCn", int32_t, count, int32_t, maxcpu)

INSTR_2ARG(instr_thread_create, "OHC", int32_t, cpu, uint64_t, tag)
INSTR_3ARG(instr_thread_execute, "OHx", int32_t, cpu, int32_t, creator_tid, uint64_t, tag)
INSTR_0ARG(instr_thread_pause, "OHp")
INSTR_0ARG(instr_thread_resume, "OHr")
INSTR_0ARG(instr_thread_cool, "OHc")
INSTR_0ARG(instr_thread_warm, "OHw")

#ifdef ENABLE_INSTRUMENTATION

static inline void instr_cpu_id(int index, int phyid)
{
	ovni_add_cpu(index, phyid);
}

static inline void instr_thread_end(void)
{
	struct ovni_ev ev = {0};

	ovni_ev_set_clock(&ev, ovni_clock_now());
	ovni_ev_set_mcv(&ev, "OHe");
	ovni_ev_emit(&ev);

	// Flush the events to disk before killing the thread
	ovni_flush();
}

static inline void instr_proc_init(void)
{
	char hostname[HOST_NAME_MAX + 1];
	int appid;
	char *appid_str;

	if (gethostname(hostname, HOST_NAME_MAX) != 0) {
		nosv_abort("Could not get hostname while initializing instrumentation");
	}

	// gethostname() may not null-terminate the buffer
	hostname[HOST_NAME_MAX] = '\0';

	if ((appid_str = getenv("NOSV_APPID")) == NULL) {
		nosv_warn("NOSV_APPID not set, using 1 as default");
		appid = 1;
	} else {
		errno = 0;
		appid = strtol(appid_str, NULL, 10);
		if (errno != 0)
			nosv_abort("strtol() failed to parse NOSV_APPID as a number");
		if (appid <= 0)
			nosv_abort("NOSV_APPID must be larger than 0");
	}

	ovni_proc_init(appid, hostname, getpid());
}

static inline void instr_proc_fini(void)
{
	ovni_proc_fini();
}

static inline void instr_gen_bursts(void)
{
	for (int i = 0; i < 100; i++)
		instr_burst();
}

static inline void instr_thread_init(void)
{
	ovni_thread_init(gettid());
}

static inline void instr_thread_attach(void)
{
	struct ovni_ev ev = {0};

	if (!ovni_thread_isready())
		nosv_abort("The current thread is not instrumented in nosv_attach()");

	ovni_ev_set_clock(&ev, ovni_clock_now());
	ovni_ev_set_mcv(&ev, "VHa");
	ovni_ev_emit(&ev);
}

static inline void instr_thread_detach(void)
{
	struct ovni_ev ev = {0};

	ovni_ev_set_clock(&ev, ovni_clock_now());
	ovni_ev_set_mcv(&ev, "VHA");
	ovni_ev_emit(&ev);

	// Flush the events to disk before detaching the thread
	ovni_flush();
}

#else // ENABLE_INSTRUMENTATION

static inline void instr_cpu_id(int index, int phyid)
{
}
static inline void instr_thread_end(void)
{
}
static inline void instr_proc_init(void)
{
}
static inline void instr_proc_fini(void)
{
}
static inline void instr_gen_bursts(void)
{
}
static inline void instr_thread_init(void)
{
}
static inline void instr_thread_attach(void)
{
}
static inline void instr_thread_detach(void)
{
}

#endif // ENABLE_INSTRUMENTATION

#endif // INSTR_H
