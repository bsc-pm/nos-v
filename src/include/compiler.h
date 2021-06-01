/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef COMPILER_H
#define COMPILER_H

#define __constructor __attribute__((constructor))
#define __destructor __attribute__((destructor))

#define __maybe_unused __attribute__((unused))

#define unlikely(x) __builtin_expect(!!(x), 0)

#define __public __attribute__((visibility("default")))
#define __internal __attribute__((visibility("hidden")))

#endif // COMPILER_H