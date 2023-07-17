/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_ERROR_H
#define NOSV_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nosv_err {
	/* Success code is guaranteed to be zero */
	NOSV_SUCCESS = 0,
	/* All errors are non-zero */
	NOSV_ERR_INVALID_CALLBACK,
	NOSV_ERR_INVALID_METADATA_SIZE,
	NOSV_ERR_INVALID_OPERATION,
	NOSV_ERR_INVALID_PARAMETER,
	NOSV_ERR_NOT_INITIALIZED,
	NOSV_ERR_OUT_OF_MEMORY,
	NOSV_ERR_OUTSIDE_TASK,
	NOSV_ERR_UNKNOWN,
	/* Only used by the runtime, keep at the end */
	NOSV_ERR_MAX,
} nosv_err_t;

/* Returns the error string corresponding to the error code. If not recognized,
   the function will return the string "Error code not recognized". Do not free
   the string memory */
const char *nosv_get_error_string(int error_code);

#ifdef __cplusplus
}
#endif

#endif // NOSV_ERROR_H
