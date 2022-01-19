/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include <stdarg.h>
#include <stdio.h>

typedef struct tap {
	int ntests;
} tap_t;

static inline void tap_init(tap_t *tap)
{
	tap->ntests = 0;
}

static inline void tap_ok(tap_t *tap, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	tap->ntests++;

	printf("ok - ");
	vprintf(fmt, args);
	printf("\n");
}

static inline void tap_fail(tap_t *tap, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	tap->ntests++;

	printf("not ok - ");
	vprintf(fmt, args);
	printf("\n");
}

static inline void tap_xfail(tap_t *tap, const char *reason, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	tap->ntests++;

	printf("not ok - ");
	vprintf(fmt, args);
	printf(" # TODO %s\n", reason);
}

static inline void tap_skip(tap_t *tap, const char *reason, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	tap->ntests++;

	printf("not ok - ");
	vprintf(fmt, args);
	printf(" # SKIP %s\n", reason);
}

static inline void tap_error(tap_t *tap, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	tap->ntests++;

	printf("Bail out! ");
	vprintf(fmt, args);
	printf("\n");
}

static inline void tap_end(tap_t *tap)
{
	printf("1..%d\n", tap->ntests);
}
