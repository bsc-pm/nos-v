/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <nosv.h>

static const char *errors[-NOSV_ERR_MAX] = {
	[-NOSV_SUCCESS] =                   "Operation succeeded",
	[-NOSV_ERR_INVALID_CALLBACK] =      "Invalid callback",
	[-NOSV_ERR_INVALID_METADATA_SIZE] = "Task metadata size is too large",
	[-NOSV_ERR_INVALID_OPERATION] =     "Invalid operation",
	[-NOSV_ERR_INVALID_PARAMETER] =     "Invalid parameter",
	[-NOSV_ERR_NOT_INITIALIZED] =       "Runtime not initialized yet or already shutdown",
	[-NOSV_ERR_OUT_OF_MEMORY] =         "Failed to allocate memory",
	[-NOSV_ERR_OUTSIDE_TASK] =          "Must run in a task context",
	[-NOSV_ERR_UNKNOWN] =               "Unknown error",
};

const char *nosv_get_error_string(int error_code)
{
	if (error_code < 0)
		error_code *= -1;

	if (error_code >= -NOSV_ERR_MAX)
		return "Error code not recognized";

	assert(errors[error_code]);
	return errors[error_code];
}
