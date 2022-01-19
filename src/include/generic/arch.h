/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef ARCH_H
#define ARCH_H

/*
	This header contains all architecture-specific macros, which are selectively included from generic/arch/
	Note that implementing a new function here should include either implementations for all architectures or
	a suitable "generic" fallback.
*/

// PowerPC
#if defined(__powerpc__) || defined(__powerpc64__) || defined(__PPC__) || defined(__PPC64__) || defined(_ARCH_PPC) || defined(_ARCH_PPC64)
#include "arch/power.h"
#endif

// x86
#if defined(__i386__) || defined(__x86_64__)
#include "arch/x86.h"
#endif

// arm64 / aarch64
#if defined(__arm__) || defined(__aarch64__)
#include "arch/arm64.h"
#endif

#ifndef ARCH_SUPPORTED
#error "Unsupported architecture"
#endif

static inline void spin_wait(void)
{
	arch_spin_wait_body();
}

static inline void spin_wait_release(void)
{
	arch_spin_wait_release();
}

#ifdef ARCH_HAS_TURBO
#define arch_enable_turbo() __arch_enable_turbo()
#else
#define arch_enable_turbo() do {} while (0)
#endif

#endif // ARCH_H
