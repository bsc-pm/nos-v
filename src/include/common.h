/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef COMMON_H
#define COMMON_H

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "compiler.h"

#define _errstr(fmt, ...) fprintf(stderr, fmt "%s\n", ##__VA_ARGS__)

#define _printstr(fmt, ...) printf(fmt "%s\n", ##__VA_ARGS__)

#define nosv_warn(...) _nosv_warn(__VA_ARGS__, "")

#define _nosv_warn(fmt, ...) \
	_errstr("NOS-V WARNING in %s(): " fmt, __func__, ##__VA_ARGS__);

#define nosv_abort(...) _nosv_abort(__VA_ARGS__, "")

#define _nosv_abort(fmt, ...)                                                                      \
	do {                                                                                           \
		if (errno)                                                                                 \
			_errstr("NOS-V ERROR in %s(): " fmt ": %s", __func__, ##__VA_ARGS__, strerror(errno)); \
		else                                                                                       \
			_errstr("NOS-V ERROR in %s(): " fmt, __func__, ##__VA_ARGS__);                         \
		exit(1);                                                                                   \
	} while (0)

#define nosv_print(...) _nosv_print(__VA_ARGS__, "")

#define _nosv_print(fmt, ...) \
	_printstr(fmt, ##__VA_ARGS__);

static inline size_t next_power_of_two(uint64_t n)
{
	return 64 - __builtin_clzll(n - 1);
}

#endif // COMMON_H
