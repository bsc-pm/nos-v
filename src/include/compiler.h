/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef COMPILER_H
#define COMPILER_H

#include "climits.h"

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

#endif // COMPILER_H