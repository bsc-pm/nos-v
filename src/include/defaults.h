/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef DEFAULTS_H
#define DEFAULTS_H

#include <config.h>

/*
	This file is intended to contain all the static limits that may need to be edited
	in some cases by nOS-V users.
*/

// GENERAL LIMITS

// Maximum number of CPUs
#define NR_CPUS 256

// Maximum number of concurrent nOS-V processes
#define MAX_PIDS 512

// Max served tasks by delegation
#define MAX_SERVED_TASKS 1024

// MISC

// Thread stack size (8MB)
#define THREAD_STACK_SIZE (1ULL << 23)

// SHARED MEMORY

#define SHM_START_ADDR ((void *) 0x0000200000000000)
#define SHM_SIZE (1ULL << 31)
#define SHM_NAME "nosv"
#define SHM_ISOLATION_LEVEL "user"

// SCHEDULER

// Default CPUs per SPSC queue in the scheduler
// This parameter can be tuned to define how many CPUs share a single queue
// 1 means each CPU has its own queue, 4 means there is a lock-protected queue
// each 4 CPUs
#define SCHED_MPSC_CPU_BATCH 1

// Default batch size for pulling tasks out of incoming queues
#define SCHED_BATCH_SIZE 64

// Quantum
#define SCHED_QUANTUM_NS (20ULL * 1000ULL * 1000ULL)

// Size of each SPSC queue
#define SCHED_IN_QUEUE_SIZE 256

// CPU MANAGER

// The default CPU binding mask
#define TOPOLOGY_BINDING "inherit"

// AFFINITY

// Default task affinity values
#define TASK_AFFINITY_DEFAULT "all"
#define TASK_AFFINITY_DEFAULT_POLICY "strict"

// Default thread affinity value
#define THREAD_AFFINITY_COMPAT_SUPPORT 1

// HARDWARE COUNTERS

// Whether to print verbose info of hwcounters, disabled by default
#define HWCOUNTERS_VERBOSE 0

// The enabled HWCounter backend, "none" by default
#define HWCOUNTERS_BACKEND "none"

// Enabled instrumentation. None by default
#define INSTR_DEFAULT_VERSION "none"

// How many idle spins will a lock have before it is considered "idling"
#define IDLE_SPINS_THRESHOLD 1

// INSTRUMENTATION

// Size in bytes of the ring buffer for kernel events (per thread)
#define INSTR_OVNI_KERNEL_RINGSIZE (4ULL * 1024ULL * 1024ULL) // 4 MB

#endif // DEFAULTS_H
