/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"

#include <nosv.h>

int main() {
	test_t test;

	test_init(&test, 2);

	nosv_init();
	test_ok(&test, "Can initialize nOS-V");

	nosv_shutdown();
	test_ok(&test, "Can shutdown nOS-V");
}
