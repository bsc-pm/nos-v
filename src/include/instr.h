#ifndef INSTR_H
#define INSTR_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

#ifdef ENABLE_INSTRUMENTATION
#include <ovni.h>
#endif

#include "nosv-internal.h"
#include "hardware/threads.h"
#include "hardware/pids.h"
#include "api/nosv.h"

#ifdef ENABLE_INSTRUMENTATION
# define INSTR_BEGIN ovni_clock_update(); do {
# define INSTR_END } while (0);
#endif

#ifdef ENABLE_INSTRUMENTATION
#define INSTR_0ARG(name, mcv) \
	static inline void name(){ \
		INSTR_BEGIN \
		struct ovni_ev ev = {0}; \
		ovni_ev_set_mcv(&ev, mcv); \
		ovni_ev(&ev); \
		INSTR_END \
	}

#define INSTR_1ARG(name, mcv, ta, a) \
	static inline void name(ta a){ \
		INSTR_BEGIN \
		struct ovni_ev ev = {0}; \
		ovni_ev_set_mcv(&ev, mcv); \
		ovni_payload_add(&ev, (uint8_t *) &a, sizeof(a)); \
		ovni_ev(&ev); \
		INSTR_END \
	}

#define INSTR_2ARG(name, mcv, ta, a, tb, b) \
	static inline void name(ta a, tb b){ \
		INSTR_BEGIN \
		struct ovni_ev ev = {0}; \
		ovni_ev_set_mcv(&ev, mcv); \
		ovni_payload_add(&ev, (uint8_t *) &a, sizeof(a)); \
		ovni_payload_add(&ev, (uint8_t *) &b, sizeof(b)); \
		ovni_ev(&ev); \
		INSTR_END \
	}

#define INSTR_3ARG(name, mcv, ta, a, tb, b, tc, c) \
	static inline void name(ta a, tb b, tc c){ \
		INSTR_BEGIN \
		struct ovni_ev ev = {0}; \
		ovni_ev_set_mcv(&ev, mcv); \
		ovni_payload_add(&ev, (uint8_t *) &a, sizeof(a)); \
		ovni_payload_add(&ev, (uint8_t *) &b, sizeof(b)); \
		ovni_payload_add(&ev, (uint8_t *) &c, sizeof(c)); \
		ovni_ev(&ev); \
		INSTR_END \
	}
#else
#define INSTR_0ARG(name, mcv) \
	static inline void name(){ \
	}

#define INSTR_1ARG(name, mcv, ta, a) \
	static inline void name(ta a){ \
	}

#define INSTR_2ARG(name, mcv, ta, a, tb, b) \
	static inline void name(ta a, tb b){ \
	}

#define INSTR_3ARG(name, mcv, ta, a, tb, b, tc, c) \
	static inline void name(ta a, tb b, tc c){ \
	}
#endif

/* nOS-V events */

INSTR_0ARG(instr_sched_recv,            "VSr")
INSTR_0ARG(instr_sched_send,            "VSs")
INSTR_0ARG(instr_sched_self_assign,     "VS@")
INSTR_0ARG(instr_sched_submit_enter,    "VU[")
INSTR_0ARG(instr_sched_submit_exit,     "VU]")

INSTR_0ARG(instr_salloc_enter,          "VM[")
INSTR_0ARG(instr_salloc_exit,           "VM]")

INSTR_2ARG(instr_task_create,           "VTc", int32_t, task_id, int32_t, type_id)
INSTR_1ARG(instr_task_execute,          "VTx", int32_t, task_id)
INSTR_1ARG(instr_task_pause,            "VTp", int32_t, task_id)
INSTR_1ARG(instr_task_resume,           "VTr", int32_t, task_id)
INSTR_1ARG(instr_task_end,              "VTe", int32_t, task_id)

/* A jumbo event is needed to encode a large label */
static inline void instr_type_create(int typeid, const char *label)
{
#ifdef ENABLE_INSTRUMENTATION
	INSTR_BEGIN

#define JUMBO_BUFSIZE 1024

	size_t bufsize;
	struct ovni_ev ev = {0};
	uint8_t buf[JUMBO_BUFSIZE], *p;

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

	INSTR_END
#endif
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
#endif
}


INSTR_2ARG(instr_thread_create,         "OHc", int32_t, cpu, void *, arg)
INSTR_3ARG(instr_thread_execute,        "OHx", int32_t, cpu, int32_t, creator_tid, void *, arg)
INSTR_0ARG(instr_thread_pause,          "OHp")
INSTR_0ARG(instr_thread_resume,         "OHr")

static inline void instr_thread_end()
{
#ifdef ENABLE_INSTRUMENTATION
	INSTR_BEGIN
	struct ovni_ev ev = {0};

	ovni_ev_set_mcv(&ev, "OHe");
	ovni_ev(&ev);

	/* Flush the events to disk before killing the thread */
	ovni_flush();
	INSTR_END
#endif
}

static inline void
instr_proc_init()
{
#ifdef ENABLE_INSTRUMENTATION
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
#endif
}

static inline void
instr_proc_fini()
{
#ifdef ENABLE_INSTRUMENTATION
	ovni_proc_fini();
#endif
}

static inline void
instr_gen_bursts()
{
#ifdef ENABLE_INSTRUMENTATION
	int i;

	for(i=0; i<100; i++)
		instr_burst();
#endif
}

static inline void
instr_thread_init()
{
#ifdef ENABLE_INSTRUMENTATION
	ovni_thread_init(syscall(SYS_gettid));
#endif
}

#endif
