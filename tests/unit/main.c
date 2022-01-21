/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2022 Barcelona Supercomputing Center (BSC)
*/

#include <stdio.h>

#include "test.h"
#include "unit.h"

struct test_state test_state;

int main(int argc, char *argv[]) {
	test_t test;
	test_init(&test, test_state.n_tests);
	test_option(&test, TEST_OPTION_PARALLEL, 0);

	for (int i = 0; i < test_state.n_tests; ++i) {
		int result = 0;
		test_state.tests[i].function(&result);

		test_check(&test, !result, "Unit test %s", test_state.tests[i].str);
	}

	test_end(&test);
}
