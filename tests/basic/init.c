/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"

#include <nosv.h>

int main() {
	test_t test;

	test_init(&test, 2);

	int err = nosv_init();
	test_check(&test, !err, "Can initialize nOS-V");

	err = nosv_shutdown();
	test_check(&test, !err, "Can shutdown nOS-V");
}
