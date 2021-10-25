#ifndef INSTR_H
#define INSTR_H

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef ENABLE_INSTRUMENTATION
# include <ovni.h>
#endif

#include "common.h"
#include "compat.h"
#include "nosv-internal.h"
#include "hardware/threads.h"
#include "hardware/pids.h"
#include "api/nosv.h"

#ifdef ENABLE_INSTRUMENTATION
# define INSTR_0ARG(name, mcv) \
	static inline void name(){ \
		ovni_clock_update(); \
		struct ovni_ev ev = {0}; \
		ovni_ev_set_mcv(&ev, mcv); \
		ovni_ev(&ev); \
	}

# define INSTR_1ARG(name, mcv, ta, a) \
	static inline void name(ta a){ \
		ovni_clock_update(); \
		struct ovni_ev ev = {0}; \
		ovni_ev_set_mcv(&ev, mcv); \
		ovni_payload_add(&ev, (uint8_t *) &a, sizeof(a)); \
		ovni_ev(&ev); \
	}

# define INSTR_2ARG(name, mcv, ta, a, tb, b) \
	static inline void name(ta a, tb b){ \
		ovni_clock_update(); \
		struct ovni_ev ev = {0}; \
		ovni_ev_set_mcv(&ev, mcv); \
		ovni_payload_add(&ev, (uint8_t *) &a, sizeof(a)); \
		ovni_payload_add(&ev, (uint8_t *) &b, sizeof(b)); \
		ovni_ev(&ev); \
	}

# define INSTR_3ARG(name, mcv, ta, a, tb, b, tc, c) \
	static inline void name(ta a, tb b, tc c){ \
		ovni_clock_update(); \
		struct ovni_ev ev = {0}; \
		ovni_ev_set_mcv(&ev, mcv); \
		ovni_payload_add(&ev, (uint8_t *) &a, sizeof(a)); \
		ovni_payload_add(&ev, (uint8_t *) &b, sizeof(b)); \
		ovni_payload_add(&ev, (uint8_t *) &c, sizeof(c)); \
		ovni_ev(&ev); \
	}
#else // ENABLE_INSTRUMENTATION
# define INSTR_0ARG(name, mcv) \
	static inline void name() { }

# define INSTR_1ARG(name, mcv, ta, a) \
	static inline void name(ta a) { }

# define INSTR_2ARG(name, mcv, ta, a, tb, b) \
	static inline void name(ta a, tb b) { }

# define INSTR_3ARG(name, mcv, ta, a, tb, b, tc, c) \
	static inline void name(ta a, tb b, tc c) { }
#endif // ENABLE_INSTRUMENTATION

/* ----------------------- nOS-V events  --------------------------- */

INSTR_0ARG(instr_code_enter,            "VC[")
INSTR_0ARG(instr_code_exit,             "VC]")

INSTR_0ARG(instr_sched_recv,            "VSr")
INSTR_0ARG(instr_sched_send,            "VSs")
INSTR_0ARG(instr_sched_self_assign,     "VS@")
INSTR_0ARG(instr_sched_hungry,          "VSh")
INSTR_0ARG(instr_sched_fill,            "VSf")
INSTR_0ARG(instr_sched_server_enter,    "VS[")
INSTR_0ARG(instr_sched_server_exit,     "VS]")

INSTR_0ARG(instr_sched_submit_enter,    "VU[")
INSTR_0ARG(instr_sched_submit_exit,     "VU]")

INSTR_0ARG(instr_salloc_enter,          "VMa")
INSTR_0ARG(instr_salloc_exit,           "VMA")
INSTR_0ARG(instr_sfree_enter,           "VMf")
INSTR_0ARG(instr_sfree_exit,            "VMF")

INSTR_0ARG(instr_submit_enter,          "VAs")
INSTR_0ARG(instr_submit_exit,           "VAS")
INSTR_0ARG(instr_pause_enter,           "VAp")
INSTR_0ARG(instr_pause_exit,            "VAP")
INSTR_0ARG(instr_yield_enter,           "VAy")
INSTR_0ARG(instr_yield_exit,            "VAY")
INSTR_0ARG(instr_waitfor_enter,         "VAw")
INSTR_0ARG(instr_waitfor_exit,          "VAW")
INSTR_0ARG(instr_schedpoint_enter,      "VAc")
INSTR_0ARG(instr_schedpoint_exit,       "VAC")

INSTR_2ARG(instr_task_create,           "VTc", uint32_t, task_id, uint32_t, type_id)
INSTR_1ARG(instr_task_execute,          "VTx", uint32_t, task_id)
INSTR_1ARG(instr_task_pause,            "VTp", uint32_t, task_id)
INSTR_1ARG(instr_task_resume,           "VTr", uint32_t, task_id)
INSTR_1ARG(instr_task_end,              "VTe", uint32_t, task_id)

#ifdef ENABLE_INSTRUMENTATION

/* A jumbo event is needed to encode a large label */
static inline void instr_type_create(uint32_t typeid, const char *label)
{
	ovni_clock_update();

#define JUMBO_BUFSIZE 1024

	size_t bufsize, label_len, size_left;
	struct ovni_ev ev = {0};
	uint8_t buf[JUMBO_BUFSIZE], *p;

	p = buf;
	size_left = sizeof(buf);
	bufsize = 0;

	memcpy(p, &typeid, sizeof(typeid));
	p += sizeof(typeid);
	size_left -= sizeof(typeid);
	bufsize += sizeof(typeid);

	if(label == NULL)
		label = "";

	label_len = strlen(label);

	// Truncate the label if required
	if(label_len > size_left - 1) {
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

	ovni_ev_set_mcv(&ev, "VYc");
	ovni_ev_jumbo(&ev, buf, bufsize);

#undef JUMBO_BUFSIZE
}

#else // ENABLE_INSTRUMENTATION

static inline void instr_type_create(uint32_t typeid, const char *label) {}

#endif // ENABLE_INSTRUMENTATION

/* ----------------------- Ovni events  --------------------------- */

INSTR_0ARG(instr_burst,                 "OB.")

INSTR_1ARG(instr_affinity_set,          "OAs", int32_t, cpu)
INSTR_2ARG(instr_affinity_remote,       "OAr", int32_t, cpu, int32_t, tid)

INSTR_2ARG(instr_cpu_count,             "OCn", int32_t, count, int32_t, maxcpu)

INSTR_2ARG(instr_thread_create,         "OHC", int32_t, cpu, void *, arg)
INSTR_3ARG(instr_thread_execute,        "OHx", int32_t, cpu, int32_t, creator_tid, void *, arg)
INSTR_0ARG(instr_thread_pause,          "OHp")
INSTR_0ARG(instr_thread_resume,         "OHr")
INSTR_0ARG(instr_thread_cool,           "OHc")
INSTR_0ARG(instr_thread_warm,           "OHw")

#ifdef ENABLE_INSTRUMENTATION

static inline void instr_cpu_id(int index, int phyid)
{
	ovni_add_cpu(index, phyid);
}

static inline void instr_thread_end()
{
	ovni_clock_update();
	struct ovni_ev ev = {0};

	ovni_ev_set_mcv(&ev, "OHe");
	ovni_ev(&ev);

	/* Flush the events to disk before killing the thread */
	ovni_flush();
}

static inline void instr_proc_init()
{
	ovni_clock_update();

	char hostname[HOST_NAME_MAX + 1];
	int appid;
	char *appid_str;

	if (gethostname(hostname, HOST_NAME_MAX) != 0) {
		nosv_abort("Could not get hostname while initializing instrumentation");
	}

	/* gethostname() may not null-terminate the buffer */
	hostname[HOST_NAME_MAX] = '\0';

	if ((appid_str = getenv("NOSV_APPID")) == NULL) {
		nosv_warn("NOSV_APPID not set, using 0 as default");
		appid = 0;
	} else {
		errno = 0;
		appid = strtol(appid_str, NULL, 10);
		if (errno != 0)
			nosv_abort("strtol() failed to parse NOSV_APPID as a number");
	}

	ovni_proc_init(appid, hostname, getpid());
}

static inline void instr_proc_fini()
{
	ovni_proc_fini();
}

static inline void instr_gen_bursts()
{
	int i;

	for(i=0; i<100; i++)
		instr_burst();
}

static inline void instr_thread_init()
{
	ovni_thread_init(gettid());
}

#else // ENABLE_INSTRUMENTATION

static inline void instr_cpu_id(int index, int phyid) {}
static inline void instr_thread_end() {}
static inline void instr_proc_init() {}
static inline void instr_proc_fini() {}
static inline void instr_gen_bursts() {}
static inline void instr_thread_init() {}

#endif // ENABLE_INSTRUMENTATION

#endif // INSTR_H
