/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"

#include <nosv.h>

int main() {
	test_t test;

	test_init(&test, 4);

	int err = nosv_init();
	test_check(&test, !err, "Can initialize nOS-V");

	err = nosv_shutdown();
	test_check(&test, !err, "Can shutdown nOS-V");

	err = nosv_init();
	test_check(&test, !err, "Can re-initialize nOS-V");

	err = nosv_shutdown();
	test_check(&test, !err, "Can re-shutdown nOS-V");
}
