/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef ARCH_H
#define ARCH_H

#if defined(__powerpc__) || defined(__powerpc64__) || defined(__PPC__) || defined(__PPC64__) || defined(_ARCH_PPC) || defined(_ARCH_PPC64)
#define ARCH_POWER9
#endif

#if defined(__i386__) || defined(__x86_64__)
#define ARCH_X86
#endif

#if defined(__arm__) || defined(__aarch64__)
#define ARCH_ARM64
#endif

#ifdef ARCH_POWER9
/* Macros for adjusting thread priority (hardware multi-threading) */
#define HMT_very_low()    asm volatile("or 31,31,31   # very low priority")
#define HMT_low()         asm volatile("or 1,1,1	  # low priority")
#define HMT_medium_low()  asm volatile("or 6,6,6	  # medium low priority")
#define HMT_medium()      asm volatile("or 2,2,2	  # medium priority")
#define HMT_medium_high() asm volatile("or 5,5,5	  # medium high priority")
#define HMT_high()        asm volatile("or 3,3,3	  # high priority")
#define HMT_barrier()     asm volatile("" : : : "memory")
#endif

static inline void spin_wait()
{
#ifdef ARCH_POWER9
	HMT_low();
#elif defined(ARCH_X86)
	__asm__ __volatile__("pause" ::: "memory");
#elif defined(ARCH_ARM64)
	__asm__ __volatile__ ("yield");
#else
	#error "Unsupported architecture"
#endif
}

static inline void spin_wait_release()
{
#ifdef ARCH_POWER9
	HMT_medium();
#endif
}


#endif // ARCH_H