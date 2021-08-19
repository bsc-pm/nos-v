/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef ARCH_ARM64_H
#define ARCH_ARM64_H

#define ARCH_ARM64
#define ARCH_SUPPORTED

#if defined(ARCH_X86) || defined(ARCH_POWER9)
#error "Multiple architecture definitions"
#endif

#define arch_spin_wait_body() __asm__ __volatile__ ("yield")
#define arch_spin_wait_release()

// Note: this *assumes* LSE support. If this fails to compile, either you forgot
// to specify a -march= flag to the compiler, or you are on ARMv8.0. In the last
// case, you can remove the following define
#define ARCH_HAS_DWCAS

#define cmpxchg_double(addr1, addr2, old1, old2, new1, new2)                                       \
	({                                                                                             \
		register uint64_t __old1 __asm("x0") = (uint64_t)(old1);                                   \
		register uint64_t __old2 __asm("x1") = (uint64_t)(old2);                                   \
		register uint64_t __new1 __asm("x2") = (uint64_t)(new1);                                   \
		register uint64_t __new2 __asm("x3") = (uint64_t)(new2);                                   \
		static_assert(sizeof(*(addr1)) == sizeof(*(addr2)));                                       \
		static_assert(sizeof(*(addr1)) == sizeof(long));                                           \
		__asm__ __volatile__(                                                                      \
			"caspal %[old1], %[old2], %[new1], %[new2], %[ptr]\n\t"                                \
			: [ old1 ] "+r"(__old1), [ old2 ] "+r"(__old2), [ ptr ] "+m"(*(addr1)), "+m"(*(addr2)) \
			: [ new1 ] "r"(__new1), [ new2 ] "r"(__new2));                                         \
		(__old1 == old1 && __old2 == old2);	                                                       \
	})

#endif // ARCH_ARM64_H