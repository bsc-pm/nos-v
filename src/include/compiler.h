/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef COMPILER_H
#define COMPILER_H

#include <stdatomic.h>

#include "defaults.h"

#define __cacheline_aligned __attribute__((aligned(CACHELINE_SIZE)))

#define __constructor __attribute__((constructor))
#define __destructor __attribute__((destructor))

#define __maybe_unused __attribute__((unused))

// Indicates to the compiler that the condition is not likely to be true,
// which affects codegen
#define unlikely(x) __builtin_expect(!!(x), 0)

// nOS-V Symbols in headers must be explicitely marked.
// A __public symbol is exported and can be called by programs linking to the library
// While an __internal symbol is local to the DSO domain.
#define __public __attribute__((visibility("default")))
#define __internal __attribute__((visibility("hidden")))

// _Thread_local is the C11 keyword, but we define a macro just in case
// we may need to support older compilers
#define thread_local _Thread_local

// Fallthrough signals a situation in a switch statement where the lack of a
// break at the end of a case is intended.
// For GCC < 7, the attribute is not present and it is instead substituted by an empty
// statement.
#if __has_attribute(fallthrough)
	#define fallthrough __attribute__((fallthrough))
#else
	#define fallthrough do {} while (0)
#endif

// Define some wrappers for GNU extensions in ISO C
#ifndef typeof
#define typeof __typeof__
#endif

// Define standard atomic int types
typedef atomic_uint_fast64_t atomic_uint64_t;
_Static_assert(sizeof(atomic_uint64_t) == 8, "atomic_uint64_t must be 64-bit");
typedef atomic_uint_least32_t atomic_uint32_t;
_Static_assert(sizeof(atomic_uint32_t) == 4, "atomic_uint32_t must be 32-bit");

#endif // COMPILER_H
