/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef UNIT_H
#define UNIT_H

#include <assert.h>
#include <stdlib.h>

typedef void (*ts_test_func)(int *);

struct ts_test {
	const char *str;
	ts_test_func function;
};

struct test_state {
	int n_tests;
	struct ts_test *tests;
};

#define TEST_CASE(name)                                                                                         \
	extern struct test_state test_state;                                                                        \
	static void _test_##name(int *test_result);                                                                 \
	static void _test_wrapper_##name(int *test_result)                                                          \
	{                                                                                                           \
		_test_##name(test_result);                                                                              \
	}                                                                                                           \
	__attribute__((constructor)) static void _register_test_##name()                                            \
	{                                                                                                           \
		int test_n = test_state.n_tests++;                                                                      \
		test_state.tests = (struct ts_test *) realloc(test_state.tests, sizeof(struct ts_test) * (test_n + 1)); \
		test_state.tests[test_n].str = #name;                                                                   \
		test_state.tests[test_n].function = _test_wrapper_##name;                                               \
	}                                                                                                           \
	static void _test_##name(int *test_result)


#define _EXPECT(x, y, op)     \
	do {                      \
		if (!((x) op(y))) {   \
			*test_result = 1; \
		}                     \
	} while (0)

#define EXPECT_TRUE(x) _EXPECT(!!(x), 1, ==)
#define EXPECT_FALSE(x) _EXPECT(!!(x), 0, ==)
#define EXPECT_EQ(x, y) _EXPECT((x), (y), ==)
#define EXPECT_NE(x, y) _EXPECT((x), (y), !=)
#define EXPECT_GT(x, y) _EXPECT((x), (y), >)
#define EXPECT_GE(x, y) _EXPECT((x), (y), >=)
#define EXPECT_LT(x, y) _EXPECT((x), (y), <)
#define EXPECT_LE(x, y) _EXPECT((x), (y), <=)

#endif // UNIT_H
