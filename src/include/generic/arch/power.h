/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef ARCH_POWER_H
#define ARCH_POWER_H

#define ARCH_POWER9
#define ARCH_SUPPORTED

#if defined(ARCH_X86) || defined(ARCH_ARM64)
#error "Multiple architecture definitions"
#endif

/* Macros for adjusting thread priority (hardware multi-threading) */
#define __HMT_very_low()    asm volatile("or 31,31,31   # very low priority")
#define __HMT_low()         asm volatile("or 1,1,1	  # low priority")
#define __HMT_medium_low()  asm volatile("or 6,6,6	  # medium low priority")
#define __HMT_medium()      asm volatile("or 2,2,2	  # medium priority")
#define __HMT_medium_high() asm volatile("or 5,5,5	  # medium high priority")
#define __HMT_high()        asm volatile("or 3,3,3	  # high priority")
#define __HMT_barrier()     asm volatile("" : : : "memory")

#define arch_spin_wait_body() __HMT_low()
#define arch_spin_wait_release() __HMT_medium()

#endif // ARCH_POWER_H
