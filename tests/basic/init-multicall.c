/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"

#include <nosv.h>

int main() {
	test_t test;

	test_init(&test, 8);

	int err = nosv_init();
	test_check(&test, !err, "Valid nosv_init (1): initialize");

	err = nosv_init();
	test_check(&test, !err, "Valid nosv_init (2)");

	err = nosv_shutdown();
	test_check(&test, !err, "Valid nosv_shutdown (1)");

	err = nosv_init();
	test_check(&test, !err, "Valid nosv_init (3)");

	err = nosv_shutdown();
	test_check(&test, !err, "Valid nosv_shutdown (2)");

	err = nosv_shutdown();
	test_check(&test, !err, "Valid nosv_shutdown (3): finalize");

	err = nosv_shutdown();
	test_check(&test, err, "Invalid nosv_shutdown (4), unmatching");

	err = nosv_init();
	test_check(&test, err, "Invalid nosv_init (5): re-initialize not allowed");
}
