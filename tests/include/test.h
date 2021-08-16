/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct test {
	int ntests;
	int expected;
} test_t;

static inline void test_init(test_t *test, int ntests)
{
	printf("pl%d\n", ntests);
	test->ntests = 0;
	test->expected = ntests;
}

static inline void test_ok(test_t *test, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	test->ntests++;

	printf("pa");
	vprintf(fmt, args);
	printf("\n");
}

static inline void test_fail(test_t *test, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	test->ntests++;

	printf("fa");
	vprintf(fmt, args);
	printf("\n");
}

static inline void test_xfail(test_t *test, const char *reason, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	test->ntests++;

	printf("xf");
	vprintf(fmt, args);
	printf("####%s\n", reason);
}

static inline void test_skip(test_t *test, const char *reason, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	test->ntests++;

	printf("sk");
	vprintf(fmt, args);
	printf("####%s\n", reason);
}

static inline void test_error(test_t *test, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	test->ntests++;

	printf("bo");
	vprintf(fmt, args);
	printf("\n");
}

static inline void test_end(test_t *test)
{
	assert(test->ntests == test->expected);
}
