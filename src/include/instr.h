/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef INSTR_H
#define INSTR_H

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef ENABLE_INSTRUMENTATION
#include <ovni.h>

#include <linux/perf_event.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#endif

#include "common.h"
#include "compat.h"
#include "config/config.h"
#include "hardware/pids.h"
#include "hardware/threads.h"
#include "system/tasks.h"
#include "nosv-internal.h"

#ifdef ENABLE_INSTRUMENTATION
__internal extern uint64_t instr_ovni_control;
static thread_local int instr_require_done = 0;

enum instr_bit {
	INSTR_BIT_BASIC = 0,
	INSTR_BIT_WORKER,
	INSTR_BIT_SCHEDULER,
	INSTR_BIT_SCHEDULER_SUBMIT,
	INSTR_BIT_MEMORY,
	INSTR_BIT_API_CREATE,
	INSTR_BIT_API_DESTROY,
	INSTR_BIT_API_MUTEX_LOCK,
	INSTR_BIT_API_MUTEX_TRYLOCK,
	INSTR_BIT_API_MUTEX_UNLOCK,
	INSTR_BIT_API_SUBMIT,
	INSTR_BIT_API_PAUSE,
	INSTR_BIT_API_YIELD,
	INSTR_BIT_API_WAITFOR,
	INSTR_BIT_API_SCHEDPOINT,
	INSTR_BIT_API_ATTACH,
	INSTR_BIT_TASK,
	INSTR_BIT_KERNEL,
	INSTR_BIT_MAX,
};

#define BIT(n) (1ULL<<(n))

#define INSTR_FLAG_BASIC				BIT(INSTR_BIT_BASIC)
#define INSTR_FLAG_WORKER				BIT(INSTR_BIT_WORKER)
#define INSTR_FLAG_SCHEDULER			BIT(INSTR_BIT_SCHEDULER)
#define INSTR_FLAG_SCHEDULER_SUBMIT		BIT(INSTR_BIT_SCHEDULER_SUBMIT)
#define INSTR_FLAG_MEMORY				BIT(INSTR_BIT_MEMORY)
#define INSTR_FLAG_API_CREATE			BIT(INSTR_BIT_API_CREATE)
#define INSTR_FLAG_API_DESTROY			BIT(INSTR_BIT_API_DESTROY)
#define INSTR_FLAG_API_MUTEX_LOCK		BIT(INSTR_BIT_API_MUTEX_LOCK)
#define INSTR_FLAG_API_MUTEX_TRYLOCK	BIT(INSTR_BIT_API_MUTEX_TRYLOCK)
#define INSTR_FLAG_API_MUTEX_UNLOCK		BIT(INSTR_BIT_API_MUTEX_UNLOCK)
#define INSTR_FLAG_API_SUBMIT			BIT(INSTR_BIT_API_SUBMIT)
#define INSTR_FLAG_API_PAUSE			BIT(INSTR_BIT_API_PAUSE)
#define INSTR_FLAG_API_YIELD			BIT(INSTR_BIT_API_YIELD)
#define INSTR_FLAG_API_WAITFOR			BIT(INSTR_BIT_API_WAITFOR)
#define INSTR_FLAG_API_SCHEDPOINT		BIT(INSTR_BIT_API_SCHEDPOINT)
#define INSTR_FLAG_API_ATTACH			BIT(INSTR_BIT_API_ATTACH)
#define INSTR_FLAG_TASK					BIT(INSTR_BIT_TASK)
#define INSTR_FLAG_KERNEL				BIT(INSTR_BIT_KERNEL)

#define INSTR_LEVEL_0 (INSTR_FLAG_BASIC)
#define INSTR_LEVEL_1 (INSTR_LEVEL_0 | INSTR_FLAG_WORKER | INSTR_FLAG_TASK)
#define INSTR_LEVEL_2 (INSTR_LEVEL_1 | INSTR_FLAG_SCHEDULER | INSTR_FLAG_SCHEDULER_SUBMIT \
					 | INSTR_FLAG_API_ATTACH )
#define INSTR_LEVEL_3 (INSTR_LEVEL_2 | INSTR_FLAG_API_CREATE | INSTR_FLAG_API_DESTROY \
					 | INSTR_FLAG_API_SUBMIT | INSTR_FLAG_API_PAUSE | INSTR_FLAG_API_YIELD \
					 | INSTR_FLAG_API_WAITFOR | INSTR_FLAG_API_SCHEDPOINT \
					 | INSTR_FLAG_API_MUTEX_LOCK | INSTR_FLAG_API_MUTEX_TRYLOCK \
					 | INSTR_FLAG_API_MUTEX_UNLOCK \
					 | INSTR_FLAG_KERNEL )
#define INSTR_LEVEL_4 (INSTR_LEVEL_3 | INSTR_FLAG_MEMORY)

#define CHECK_INSTR_ENABLED(name) 						 \
	if (!(instr_ovni_control & INSTR_FLAG_##name))     \
		return;

__internal void instr_parse_config(void);

#define INSTR_0ARG(type, name, mcv)                     \
	static inline void name(void)                 \
	{                                             \
		CHECK_INSTR_ENABLED(type)                       \
		struct ovni_ev ev = {0};                  \
		ovni_ev_set_clock(&ev, ovni_clock_now()); \
		ovni_ev_set_mcv(&ev, mcv);                \
		ovni_ev_emit(&ev);                        \
	}

#define INSTR_1ARG(type, name, mcv, ta, a)                      \
	static inline void name(ta a)                         \
	{                                                     \
		CHECK_INSTR_ENABLED(type)                               \
		struct ovni_ev ev = {0};                          \
		ovni_ev_set_clock(&ev, ovni_clock_now());         \
		ovni_ev_set_mcv(&ev, mcv);                        \
		ovni_payload_add(&ev, (uint8_t *) &a, sizeof(a)); \
		ovni_ev_emit(&ev);                                \
	}

#define INSTR_2ARG(type, name, mcv, ta, a, tb, b)               \
	static inline void name(ta a, tb b)                   \
	{                                                     \
		CHECK_INSTR_ENABLED(type)                               \
		struct ovni_ev ev = {0};                          \
		ovni_ev_set_clock(&ev, ovni_clock_now());         \
		ovni_ev_set_mcv(&ev, mcv);                        \
		ovni_payload_add(&ev, (uint8_t *) &a, sizeof(a)); \
		ovni_payload_add(&ev, (uint8_t *) &b, sizeof(b)); \
		ovni_ev_emit(&ev);                                \
	}

#define INSTR_3ARG(type, name, mcv, ta, a, tb, b, tc, c)        \
	static inline void name(ta a, tb b, tc c)             \
	{                                                     \
		CHECK_INSTR_ENABLED(type)                               \
		struct ovni_ev ev = {0};                          \
		ovni_ev_set_clock(&ev, ovni_clock_now());         \
		ovni_ev_set_mcv(&ev, mcv);                        \
		ovni_payload_add(&ev, (uint8_t *) &a, sizeof(a)); \
		ovni_payload_add(&ev, (uint8_t *) &b, sizeof(b)); \
		ovni_payload_add(&ev, (uint8_t *) &c, sizeof(c)); \
		ovni_ev_emit(&ev);                                \
	}
#else // ENABLE_INSTRUMENTATION
#define INSTR_0ARG(type, name, mcv)     \
	static inline void name(void) \
	{                             \
	}

#define INSTR_1ARG(type, name, mcv, ta, a) \
	static inline void name(ta a)    \
	{                                \
	}

#define INSTR_2ARG(type, name, mcv, ta, a, tb, b) \
	static inline void name(ta a, tb b)     \
	{                                       \
	}

#define INSTR_3ARG(type, name, mcv, ta, a, tb, b, tc, c) \
	static inline void name(ta a, tb b, tc c)      \
	{                                              \
	}
#endif // ENABLE_INSTRUMENTATION

// ----------------------- nOS-V events  ---------------------------

INSTR_0ARG(WORKER, instr_worker_enter, "VHw")
INSTR_0ARG(WORKER, instr_worker_exit, "VHW")
INSTR_0ARG(WORKER, instr_delegate_enter, "VHd")
INSTR_0ARG(WORKER, instr_delegate_exit, "VHD")

INSTR_0ARG(SCHEDULER, instr_sched_recv, "VSr")
INSTR_0ARG(SCHEDULER, instr_sched_send, "VSs")
INSTR_0ARG(SCHEDULER, instr_sched_self_assign, "VS@")
INSTR_0ARG(SCHEDULER, instr_sched_hungry, "VSh")
INSTR_0ARG(SCHEDULER, instr_sched_fill, "VSf")
INSTR_0ARG(SCHEDULER, instr_sched_server_enter, "VS[")
INSTR_0ARG(SCHEDULER, instr_sched_server_exit, "VS]")

INSTR_0ARG(SCHEDULER_SUBMIT, instr_sched_submit_enter, "VU[")
INSTR_0ARG(SCHEDULER_SUBMIT, instr_sched_submit_exit, "VU]")

INSTR_0ARG(MEMORY, instr_salloc_enter, "VMa")
INSTR_0ARG(MEMORY, instr_salloc_exit, "VMA")
INSTR_0ARG(MEMORY, instr_sfree_enter, "VMf")
INSTR_0ARG(MEMORY, instr_sfree_exit, "VMF")

INSTR_0ARG(API_CREATE, instr_create_enter, "VAr")
INSTR_0ARG(API_CREATE, instr_create_exit, "VAR")
INSTR_0ARG(API_DESTROY, instr_destroy_enter, "VAd")
INSTR_0ARG(API_DESTROY, instr_destroy_exit, "VAD")
INSTR_0ARG(API_MUTEX_LOCK, instr_mutex_lock_enter, "VAl")
INSTR_0ARG(API_MUTEX_LOCK, instr_mutex_lock_exit, "VAL")
INSTR_0ARG(API_MUTEX_TRYLOCK, instr_mutex_trylock_enter, "VAt")
INSTR_0ARG(API_MUTEX_TRYLOCK, instr_mutex_trylock_exit, "VAT")
INSTR_0ARG(API_MUTEX_UNLOCK, instr_mutex_unlock_enter, "VAu")
INSTR_0ARG(API_MUTEX_UNLOCK, instr_mutex_unlock_exit, "VAU")
INSTR_0ARG(API_SUBMIT, instr_submit_enter, "VAs")
INSTR_0ARG(API_SUBMIT, instr_submit_exit, "VAS")
INSTR_0ARG(API_PAUSE, instr_pause_enter, "VAp")
INSTR_0ARG(API_PAUSE, instr_pause_exit, "VAP")
INSTR_0ARG(API_YIELD, instr_yield_enter, "VAy")
INSTR_0ARG(API_YIELD, instr_yield_exit, "VAY")
INSTR_0ARG(API_WAITFOR, instr_waitfor_enter, "VAw")
INSTR_0ARG(API_WAITFOR, instr_waitfor_exit, "VAW")
INSTR_0ARG(API_SCHEDPOINT, instr_schedpoint_enter, "VAc")
INSTR_0ARG(API_SCHEDPOINT, instr_schedpoint_exit, "VAC")
INSTR_0ARG(API_ATTACH, instr_attach_exit, "VAA")
INSTR_0ARG(API_ATTACH, instr_detach_enter, "VAe")

INSTR_2ARG(TASK, instr_task_create, "VTc", uint32_t, task_id, uint32_t, type_id)
INSTR_2ARG(TASK, instr_task_create_par, "VTC", uint32_t, task_id, uint32_t, type_id)
INSTR_2ARG(TASK, instr_task_execute, "VTx", uint32_t, task_id, uint32_t, body_id)
INSTR_2ARG(TASK, instr_task_pause, "VTp", uint32_t, task_id, uint32_t, body_id)
INSTR_2ARG(TASK, instr_task_resume, "VTr", uint32_t, task_id, uint32_t, body_id)
INSTR_2ARG(TASK, instr_task_end, "VTe", uint32_t, task_id, uint32_t, body_id)

#ifdef ENABLE_INSTRUMENTATION

// A jumbo event is needed to encode a large label
static inline void instr_type_create(uint32_t typeid, const char *label)
{
	CHECK_INSTR_ENABLED(BASIC)
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

static inline uint32_t instr_get_bodyid(task_execution_handle_t handle)
{
	if (!task_is_parallel(handle.task))
		return 0;

	assert(handle.execution_id > 0); // Zero is forbidden

	// The body id matches the execution id only for parallel tasks
	return handle.execution_id;
}

#else // ENABLE_INSTRUMENTATION

static inline void instr_type_create(uint32_t typeid, const char *label)
{
}

static inline uint32_t instr_get_bodyid(task_execution_handle_t handle)
{
	return 0;
}

#endif // ENABLE_INSTRUMENTATION

// ----------------------- Ovni events  ---------------------------

INSTR_0ARG(BASIC, instr_burst, "OB.")

INSTR_1ARG(BASIC, instr_affinity_set, "OAs", int32_t, cpu)
INSTR_2ARG(BASIC, instr_affinity_remote, "OAr", int32_t, cpu, int32_t, tid)

INSTR_2ARG(BASIC, instr_cpu_count, "OCn", int32_t, count, int32_t, maxcpu)

INSTR_2ARG(BASIC, instr_thread_create, "OHC", int32_t, cpu, uint64_t, tag)
INSTR_3ARG(BASIC, instr_thread_execute, "OHx", int32_t, cpu, int32_t, creator_tid, uint64_t, tag)
INSTR_0ARG(BASIC, instr_thread_pause, "OHp")
INSTR_0ARG(BASIC, instr_thread_resume, "OHr")
INSTR_0ARG(BASIC, instr_thread_cool, "OHc")
INSTR_0ARG(BASIC, instr_thread_warm, "OHw")

#ifdef ENABLE_INSTRUMENTATION

static inline void instr_cpu_id(int index, int phyid)
{
	CHECK_INSTR_ENABLED(BASIC)
	ovni_add_cpu(index, phyid);
}

static inline void instr_thread_end(void)
{
	CHECK_INSTR_ENABLED(BASIC)
	struct ovni_ev ev = {0};

	ovni_ev_set_clock(&ev, ovni_clock_now());
	ovni_ev_set_mcv(&ev, "OHe");
	ovni_ev_emit(&ev);

	// Flush the events to disk before killing the thread
	ovni_flush();
	ovni_thread_free();
}

static inline void instr_proc_init(const char *suffix)
{
	CHECK_INSTR_ENABLED(BASIC)
	char hostname[HOST_NAME_MAX + 1];
	int appid;
	char *appid_str;

	if (gethostname(hostname, HOST_NAME_MAX) != 0) {
		nosv_abort("Could not get hostname while initializing instrumentation");
	}

	// gethostname() may not null-terminate the buffer
	hostname[HOST_NAME_MAX] = '\0';

	char loom[FILENAME_MAX];
	if (snprintf(loom, FILENAME_MAX, "%s.%s", hostname, suffix) >= FILENAME_MAX)
		nosv_abort("Error building the ovni loom name");

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

	ovni_proc_init(appid, loom, getpid());
}

static inline void instr_proc_fini(void)
{
	CHECK_INSTR_ENABLED(BASIC)
	ovni_proc_fini();
}

static inline void instr_gen_bursts(void)
{
	CHECK_INSTR_ENABLED(BASIC)
	for (int i = 0; i < 100; i++)
		instr_burst();
}

static inline void instr_thread_require(void)
{
	CHECK_INSTR_ENABLED(BASIC)
	if (!instr_require_done) {
		ovni_thread_require("nosv", "2.1.0");

		if (instr_ovni_control & INSTR_FLAG_KERNEL)
			ovni_thread_require("kernel", "1.0.0");

		instr_require_done = 1;
	}
}

static inline void instr_thread_init(void)
{
	CHECK_INSTR_ENABLED(BASIC)
	ovni_thread_init(gettid());
	instr_thread_require();
}

static inline void instr_attach_enter(void)
{
	CHECK_INSTR_ENABLED(BASIC)

	if (!ovni_thread_isready())
		nosv_abort("The current thread is not instrumented in nosv_attach()");

	// Set require here, even if API_ATTACH is not enabled
	instr_thread_require();

	if (instr_ovni_control & INSTR_FLAG_API_ATTACH) {
		struct ovni_ev ev = {0};
		ovni_ev_set_clock(&ev, ovni_clock_now());
		ovni_ev_set_mcv(&ev, "VAa");
		ovni_ev_emit(&ev);
	}
}

static inline void instr_detach_exit(void)
{
	CHECK_INSTR_ENABLED(BASIC)

	if (instr_ovni_control & INSTR_FLAG_API_ATTACH) {
		struct ovni_ev ev = {0};
		ovni_ev_set_clock(&ev, ovni_clock_now());
		ovni_ev_set_mcv(&ev, "VAE");
		ovni_ev_emit(&ev);
	}

	// Flush the events to disk before detaching the thread
	ovni_flush();

	// This thread is not managed by nOS-V, so we avoid the call to
	// ovni_thread_free().
}

#else // ENABLE_INSTRUMENTATION

static inline void instr_cpu_id(int index, int phyid)
{
}
static inline void instr_thread_end(void)
{
}
static inline void instr_proc_init(const char *suffix)
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
static inline void instr_thread_require(void)
{
}
static inline void instr_attach_enter(void)
{
}
static inline void instr_detach_exit(void)
{
}

#endif // ENABLE_INSTRUMENTATION

// ----------------------- Kernel events  ---------------------------

// Must be a power of two
#define INSTR_KBUFLEN (4UL * 1024UL * 1024UL) // 4 MB

#ifdef ENABLE_INSTRUMENTATION
struct kinstr {
	int fd;
	int enabled;

	size_t bufsize;
	uint8_t *buf;

	struct perf_event_mmap_page *meta;

	size_t ringsize;
	uint8_t *ringbuf;
	uint64_t head;
	uint64_t tail;
};
#else
struct kinstr;
#endif

#ifdef ENABLE_INSTRUMENTATION
static int perf_event_open(struct perf_event_attr *attr, pid_t pid,
	int cpu, int group_fd, unsigned long flags)
{
	return (int) syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags);
}

static inline void instr_kernel_init(struct kinstr **ki_ptr)
{
	CHECK_INSTR_ENABLED(KERNEL)
	struct perf_event_attr attr;
	struct kinstr *ki;
	long pagesize;
	int cpu;
	pid_t tid;

	if ((ki = salloc(sizeof(*ki), -1)) == NULL)
		nosv_abort("salloc failed");

	*ki_ptr = ki;

	// Measure the current thread only
	tid = 0;

	// On any CPU
	cpu = -1;

	memset(&attr, 0, sizeof(attr));

	attr.size = sizeof(struct perf_event_attr);
	attr.config = PERF_COUNT_SW_DUMMY;
	attr.type = PERF_TYPE_SOFTWARE;

	attr.task = 1;
	attr.comm = 1;
	attr.freq = 0;
	attr.wakeup_events = 1;
	attr.watermark = 1;
	attr.sample_id_all = 1;
	attr.context_switch = 1;
	attr.sample_type = PERF_SAMPLE_PERIOD | PERF_SAMPLE_TIME;
	attr.use_clockid = 1;
	attr.clockid = CLOCK_MONOTONIC;

	ki->enabled = 0;
	ki->fd = perf_event_open(&attr, tid, cpu, -1, 0UL);

	if (ki->fd < 0) {
		nosv_warn("cannot enable kernel events, perf_event_open failed");
		return;
	}

	pagesize = sysconf(_SC_PAGE_SIZE);
	assert(pagesize > 0);

	ki->ringsize = INSTR_KBUFLEN;
	assert((ki->ringsize % pagesize) == 0);

	// Map the buffer: must be 1+2^n pages
	ki->bufsize = pagesize + ki->ringsize;
	ki->buf = mmap(NULL, ki->bufsize,
		PROT_READ | PROT_WRITE, MAP_SHARED, ki->fd, 0);

	if (ki->buf == MAP_FAILED) {
		nosv_warn("cannot enable kernel events, mmap failed");
		return;
	}

	// The first page is used for metadata and the next one is the data ring
	ki->meta = (struct perf_event_mmap_page *) ki->buf;
	ki->ringbuf = ki->buf + pagesize;

	ki->head = ki->meta->data_head;
	ki->tail = ki->meta->data_tail;

	ki->enabled = 1;
}

struct sample_id {
	// u32 pid, tid; }   /* if PERF_SAMPLE_TID set */
	uint64_t time; /* if PERF_SAMPLE_TIME set */
				   // u64 id;       }   /* if PERF_SAMPLE_ID set */
	// u64 stream_id;}   /* if PERF_SAMPLE_STREAM_ID set  */
	// u32 cpu, res; }   /* if PERF_SAMPLE_CPU set */
	// u64 id;       }   /* if PERF_SAMPLE_IDENTIFIER set */
};

struct perf_ev {
	struct perf_event_header header;
	struct sample_id sample;
};

static inline void emit_perf_event(struct perf_ev *ev)
{
	struct ovni_ev ovniev = {0};
	int is_out;

	if (ev->header.type != PERF_RECORD_SWITCH)
		return;

	is_out = ev->header.misc & PERF_RECORD_MISC_SWITCH_OUT;

	// Set the event clock directly from the kernel. It must use the same
	// clock (currently CLOCK_MONOTONIC).
	ovni_ev_set_clock(&ovniev, ev->sample.time);
	ovni_ev_set_mcv(&ovniev, is_out ? "KCO" : "KCI");
	ovni_ev_emit(&ovniev);
}

static inline void instr_kernel_flush(struct kinstr *ki)
{
	CHECK_INSTR_ENABLED(KERNEL)
	struct ovni_ev ev0 = {0}, ev1 = {0};
	struct perf_ev *ev;
	uint8_t *p;

	if (!ki->enabled)
		return;

	// If there are no events, do nothing
	if (ki->head == ki->meta->data_head)
		return;

	// Wrap the kernel events with special marker events, so we can sort
	// them easily at emulation
	ovni_ev_set_clock(&ev0, ovni_clock_now());
	ovni_ev_set_mcv(&ev0, "OU[");
	ovni_ev_emit(&ev0);

	while (ki->head < ki->meta->data_head) {
		p = ki->ringbuf + (ki->head % ki->ringsize);
		ev = (struct perf_ev *) p;
		emit_perf_event(ev);
		ki->head += ev->header.size;
	}

	ovni_ev_set_clock(&ev1, ovni_clock_now());
	ovni_ev_set_mcv(&ev1, "OU]");
	ovni_ev_emit(&ev1);
}

static inline void instr_kernel_free(struct kinstr *ki)
{
	CHECK_INSTR_ENABLED(KERNEL)
	sfree(ki, sizeof(*ki), -1);
}


#else // ENABLE_INSTRUMENTATION

static inline void instr_kernel_init(struct kinstr **ki_ptr)
{
}

static inline void instr_kernel_flush(struct kinstr *ki)
{
}

static inline void instr_kernel_free(struct kinstr *ki)
{
}

#endif // ENABLE_INSTRUMENTATION

#endif // INSTR_H
