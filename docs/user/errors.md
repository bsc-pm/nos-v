
# Error Handling

In nOS-V, most public functions return an integer code that indicates the successful completion of the operation, or a possible error during the function execution. When the function succeeded, `NOSV_SUCCESS` is returned. This value is guaranteed to be zero, while all errors are negative and non-zero. The `nosv/errors.h` header defines the complete API regarding the error codes and error handling. This header is automatically included by the main `nosv.h` header.

The nOS-V runtime provides the following API function to obtain the string description of a error code programmatically:

```c
const char *nosv_get_error_string(int error_code);
```

The function takes an integer error code and returns a constant string that describes the error. If the error code is not recognized, the function returns the string "Error code not recognized". The string memory should not be freed by the user in any case. This function is guaranteed to work when the runtime is not initialized or already finalized.

An example of usage could be:

```c
int err = nosv_init();
if (err != NOSV_SUCCESS) {
    fprintf(stderr, "Error in nosv_init: %s\n", nosv_get_error_string(err));
    /* handle error */
    ...
}
```

Some functions could return an error or a valid integer value (which may be zero).
In such functions, a negative returned value always implies that the function failed for some reason. In contrast, when returning zero or positive, that is the valid integer which the function should return.
In this case, it is recommended to check using the condition that errors are negative and these functions
only return valid positive values:

```c
int ret = nosv_get_current_logical_cpu();
if (ret < 0) {
    fprintf(stderr, "Error in nosv_init: %s\n", nosv_get_error_string(ret));
    /* handle error */
    ...
} else {
    // ret contains a valid logical cpu id
}
```

## Error Codes

This section briefly describes the complete list of error codes that nOS-V can return. These are:

### `NOSV_SUCCESS`

The operation succeeded with no errors. This value is guaranteed to always be zero.

### `NOSV_ERR_NOT_INITIALIZED`

The operation failed because nOS-V is not initialized. For intance, the `nosv_init` may not called yet, or a `nosv_shutdown` already finalized the runtime.

### `NOSV_ERR_INVALID_CALLBACK`

The callbacks passed when creating a task type or submitting a task are invalid. For instance, an external task type is created through `nosv_type_init` and non-NULL callbacks are passed, or when calling `nosv_attach` with non-NULL callbacks in the task type.

### `NOSV_ERR_INVALID_METADATA_SIZE`

The metadata size needed by a task (at its creation) exceeds the `NOSV_METADATA_SIZE_MAX` value (defined in `nosv.h`). This value is currently set to 4096 bytes.

### `NOSV_ERR_INVALID_OPERATION`

The operation cannot be executed in the current runtime state, in the current task, or with the passed function parameters. Some examples are that parallel tasks cannot run certain blocking operations, such as `nosv_yield`.

### `NOSV_ERR_INVALID_PARAMETER`

The function received a parameter that is invalid, such as a NULL pointer.

### `NOSV_ERR_OUT_OF_MEMORY`

The function tried to allocate memory and the memory allocator failed to provide more memory. This could occur when, for instance, the user has created an extremely high number of tasks.

### `NOSV_ERR_OUTSIDE_TASK`

The operation requires to be in a task context but the user called it from outside a task. For instance, the only tasks can run the `nosv_yield`, `nosv_pause` and `nosv_increase_event_counter` operations.

### `NOSV_ERR_UNKNOWN`

An error occurred during the function call, but the runtime does not know the reason of it. This kind of error should be rare to encounter.
