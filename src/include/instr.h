#ifndef INSTR_H
#define INSTR_H

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef gettid
# include <sys/syscall.h>
# define gettid() ((pid_t)syscall(SYS_gettid))
#endif

#ifdef ENABLE_INSTRUMENTATION
#include <ovni.h>
#endif

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
#else /* ENABLE_INSTRUMENTATION */
# define INSTR_0ARG(name, mcv) \
	static inline void name() { }

# define INSTR_1ARG(name, mcv, ta, a) \
	static inline void name(ta a) { }

# define INSTR_2ARG(name, mcv, ta, a, tb, b) \
	static inline void name(ta a, tb b) { }

# define INSTR_3ARG(name, mcv, ta, a, tb, b, tc, c) \
	static inline void name(ta a, tb b, tc c) { }
#endif /* ENABLE_INSTRUMENTATION */

/* nOS-V events */

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

INSTR_0ARG(instr_salloc_enter,          "VM[")
INSTR_0ARG(instr_salloc_exit,           "VM]")

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

INSTR_2ARG(instr_task_create,           "VTc", int32_t, task_id, int32_t, type_id)
INSTR_1ARG(instr_task_execute,          "VTx", int32_t, task_id)
INSTR_1ARG(instr_task_pause,            "VTp", int32_t, task_id)
INSTR_1ARG(instr_task_resume,           "VTr", int32_t, task_id)
INSTR_1ARG(instr_task_end,              "VTe", int32_t, task_id)

/* A jumbo event is needed to encode a large label */
static inline void instr_type_create(int typeid, const char *label)
{
#ifdef ENABLE_INSTRUMENTATION
	ovni_clock_update();

#define JUMBO_BUFSIZE 1024

	size_t bufsize;
	struct ovni_ev ev = {0};
	uint8_t buf[JUMBO_BUFSIZE], *p;

	if(label == NULL)
		label = "";

	bufsize = sizeof(typeid) + strlen(label) + 1;
	if(bufsize > JUMBO_BUFSIZE)
		abort();

	p = buf;
	memcpy(p, &typeid, sizeof(typeid));
	p += sizeof(typeid);
	memcpy(p, label, strlen(label) + 1);

	ovni_ev_set_mcv(&ev, "VYc");
	ovni_ev_jumbo(&ev, buf, bufsize);

#undef JUMBO_BUFSIZE

#endif /* ENABLE_INSTRUMENTATION */
}

/* ovni events */

INSTR_0ARG(instr_burst,                 "OB.")

INSTR_1ARG(instr_affinity_set,          "OAs", int32_t, cpu)
INSTR_2ARG(instr_affinity_remote,       "OAr", int32_t, cpu, int32_t, tid)

INSTR_2ARG(instr_cpu_count,             "OCn", int32_t, count, int32_t, maxcpu)
//INSTR_2ARG(instr_cpu_id,                "OCi", int32_t, cpu, int32_t, syscpu)

static inline void instr_cpu_id(int index, int phyid)
{
#ifdef ENABLE_INSTRUMENTATION
	ovni_add_cpu(index, phyid);
#endif /* ENABLE_INSTRUMENTATION */
}


INSTR_2ARG(instr_thread_create,         "OHC", int32_t, cpu, void *, arg)
INSTR_3ARG(instr_thread_execute,        "OHx", int32_t, cpu, int32_t, creator_tid, void *, arg)
INSTR_0ARG(instr_thread_pause,          "OHp")
INSTR_0ARG(instr_thread_resume,         "OHr")
INSTR_0ARG(instr_thread_cool,           "OHc")
INSTR_0ARG(instr_thread_warm,           "OHw")

static inline void instr_thread_end()
{
#ifdef ENABLE_INSTRUMENTATION
	ovni_clock_update();
	struct ovni_ev ev = {0};

	ovni_ev_set_mcv(&ev, "OHe");
	ovni_ev(&ev);

	/* Flush the events to disk before killing the thread */
	ovni_flush();
#endif /* ENABLE_INSTRUMENTATION */
}

static inline void
instr_proc_init()
{
#ifdef ENABLE_INSTRUMENTATION
	ovni_clock_update();

	char hostname[HOST_NAME_MAX];
	char *appid;

	if(gethostname(hostname, HOST_NAME_MAX) != 0)
	{
		perror("gethostname failed");
		abort();
	}

	if((appid = getenv("NOSV_APPID")) == NULL)
	{
		fprintf(stderr, "missing NOSV_APPID env var\n");
		abort();
	}

	ovni_proc_init(atoi(appid), hostname, getpid());
#endif /* ENABLE_INSTRUMENTATION */
}

static inline void
instr_proc_fini()
{
#ifdef ENABLE_INSTRUMENTATION
	ovni_proc_fini();
#endif /* ENABLE_INSTRUMENTATION */
}

static inline void
instr_gen_bursts()
{
#ifdef ENABLE_INSTRUMENTATION
	int i;

	for(i=0; i<100; i++)
		instr_burst();
#endif /* ENABLE_INSTRUMENTATION */
}

static inline void
instr_thread_init()
{
#ifdef ENABLE_INSTRUMENTATION
	ovni_thread_init(gettid());
#endif /* ENABLE_INSTRUMENTATION */
}

#endif /* INSTR_H */
