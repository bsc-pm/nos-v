/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#include "test.h"

#include <nosv.h>
#include <stdbool.h>
#include <string.h>

int main() {
	test_t test;
	test_init(&test, 5);

	test_check(&test, NOSV_SUCCESS == 0, "NOSV_SUCCESS is defined as zero");
	test_check(&test, NOSV_ERR_UNKNOWN < 0, "NOSV_ERR_UNKNOWN is defined as negative");

	const char *str1, *str2, *str3;
	bool ok1, ok2, ok3;

	str1 = nosv_get_error_string(NOSV_SUCCESS);
	ok1 = (str1 != NULL && strlen(str1) > 5);
	test_check(&test, ok1, "NOSV_SUCCESS string is valid");

	str2 = nosv_get_error_string(NOSV_ERR_NOT_INITIALIZED);
	ok2 = (str2 != NULL && strlen(str2) > 5);
	test_check(&test, ok2, "NOSV_ERR_NOT_INITIALIZED string is valid");

	str3 = nosv_get_error_string(20);
	ok3 = (str3 != NULL && strlen(str3) > 5);
	test_check(&test, ok3, "Unrecognized error (20) code is handled correctly");
}
