/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef ARCH_ARM64_H
#define ARCH_ARM64_H

#include <stdint.h>

#define ARCH_ARM64
#define ARCH_SUPPORTED

#if defined(ARCH_X86) || defined(ARCH_POWER9)
#error "Multiple architecture definitions"
#endif

#define arch_spin_wait_body() __asm__ __volatile__("yield")
#define arch_spin_wait_release()

#ifdef __ARM_FEATURE_ATOMICS
// Note: this *assumes* LSE support. If this fails to compile, either you forgot
// to specify a -march= flag to the compiler, or you are on ARMv8.0. In the last
// case, you can remove the following define
#define ARCH_HAS_DWCAS

#define cmpxchg_double(addr1, addr2, old1, old2, new1, new2)                                 	\
	__extension__({                                                                          	\
		register uint64_t __old1 __asm("x0") = (uint64_t)(old1);                             	\
		register uint64_t __old2 __asm("x1") = (uint64_t)(old2);                             	\
		register uint64_t __new1 __asm("x2") = (uint64_t)(new1);                             	\
		register uint64_t __new2 __asm("x3") = (uint64_t)(new2);                             	\
		register uint64_t __addr1_ptr = (uint64_t)(&(*(addr1)));                     			\
		static_assert(sizeof(*(addr1)) == sizeof(*(addr2)), "Source and dest size mismatch");	\
		static_assert(sizeof(*(addr1)) == sizeof(long), "cmpxchg must use doublewords");		\
		__asm__ __volatile__(                                                                	\
			"caspal %0, %1, %4, %5, [%6]\n\t"                                                  	\
			: [old1] "+r"(__old1), [old2] "+r"(__old2), "+m"(*(addr1)), "+m"(*(addr2)) 			\
			: [new1] "r"(__new1), [new2] "r"(__new2), [ptr] "r" (__addr1_ptr));               	\
		(__old1 == (uint64_t)old1 && __old2 == (uint64_t)old2);                              	\
	})
#else
#warning "Building on arm64 without LSE support is discouraged. Use CPPFLAGS to set the appropiate -march flag"
#endif // __ARM_FEATURE_ATOMICS

#define ARCH_HAS_TURBO
#define ARM_FPCR_FZ		(0x1000000ULL)

static inline void __arch_configure_turbo(int enabled)
{
	uint64_t fpcr = 0;

	if (enabled)
		fpcr = ARM_FPCR_FZ;

	__asm__("msr fpcr, %0" : : "r" (fpcr));
}

static inline int __arch_check_turbo(int enabled)
{
	uint64_t fpcr;
	__asm__("mrs %0, fpcr" : "=r" (fpcr));

	if (enabled)
		return !(fpcr & ARM_FPCR_FZ);
	else
		return (fpcr & ARM_FPCR_FZ);
}

#endif // ARCH_ARM64_H
