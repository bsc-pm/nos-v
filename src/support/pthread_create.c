/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2025 Barcelona Supercomputing Center (BSC)
*/

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#include "nosv.h"
#include "common.h"

typedef struct {
	void *(*start_routine)(void *);
	void *restrict arg;
} pthread_create_info_t;

void *__nosv_start_routine_wrapper(void *restrict _arg)
{
	pthread_create_info_t info = *(pthread_create_info_t *) _arg;
	free(_arg);

	nosv_task_t task;
	int err = nosv_attach(&task, NULL, NULL, NOSV_ATTACH_INSTRUMENT);
	if (err)
		nosv_abort("Error attaching thread: %s", nosv_get_error_string(err));

	void *ret = info.start_routine(info.arg);

	err = nosv_detach(NOSV_DETACH_INSTRUMENT);
	if (err)
		nosv_abort("Error detaching thread: %s", nosv_get_error_string(err));

	return ret;
}

int nosv_pthread_create(pthread_t *thread,
	const pthread_attr_t *attr,
	void *(*start_routine)(void *),
	void *arg)
{
	pthread_create_info_t *info;
	info = malloc(sizeof(*info));
	if (info == NULL)
		return EAGAIN; // Insufficient resources to create another thread.

	info->start_routine = start_routine;
	info->arg = arg;

	return pthread_create(thread, attr, __nosv_start_routine_wrapper, info);
}
