/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef ARCH_X86_H
#define ARCH_X86_H

#define ARCH_X86
#define ARCH_SUPPORTED

#if defined(ARCH_POWER9) || defined(ARCH_ARM64)
#error "Multiple architecture definitions"
#endif

#define arch_spin_wait_body() __asm__ __volatile__("pause" ::: "memory")
#define arch_spin_wait_release()

// Double Word Compare and Swap
#define ARCH_HAS_DWCAS

// Unfortunately, this has to be a macro to be """generic"""
// Both addr1 and addr2 must point to 8 bytes
// Additionally, they have to be 16-byte aligned AND contiguous in memory
// CMPXCHG16B m128 -> Compare RDX:RAX with m128. If equal, set ZF and load RCX:RBX into m128. Else, clear ZF and load m128 into RDX:RAX.
// We use the "=@ccz" idiom to tell GCC that one of the outputs IS ZF.
#define cmpxchg_double(addr1, addr2, old1, old2, new1, new2)									\
__extension__ ({																								\
	int r;																						\
	__typeof__(*(addr1)) __old1 = (old1);														\
	__typeof__(*(addr2)) __old2 = (old2);														\
	__asm__ __volatile__(																		\
		"lock cmpxchg16b %[pt]\n\t"																\
		: "=@ccz"(r), [pt] "+m"(*(addr1)), "+m"(*(addr2)), "+a"(__old1), "+d"(__old2)			\
		: "b"(new1), "c"(new2));																\
	r;																							\
})

// Define that x86 has per-thread specific "turbo" settings, to enable Denormals-Are-Zero and Flush-To-Zero in the FPU
// However, they are only available under _SSE2_
#ifdef __SSE2__
#define ARCH_HAS_TURBO

#include <pmmintrin.h>
#include <xmmintrin.h>

static inline void __arch_enable_turbo(void)
{
	_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
	_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
}
#endif // __SSE2__


#endif // ARCH_X86_H
