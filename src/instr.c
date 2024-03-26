/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023-2024 Barcelona Supercomputing Center (BSC)
*/

#include "config.h"
#include "instr.h"

uint64_t instr_ovni_control = 0;

static const uint64_t control_levels[] = {
	INSTR_LEVEL_0,
	INSTR_LEVEL_1,
	INSTR_LEVEL_2,
	INSTR_LEVEL_3,
	INSTR_LEVEL_4
};

static const uint64_t control_flags_mask[] = {
	[INSTR_BIT_BASIC] 				= INSTR_FLAG_BASIC,
	[INSTR_BIT_WORKER] 				= INSTR_FLAG_WORKER,
	[INSTR_BIT_SCHEDULER] 			= INSTR_FLAG_SCHEDULER,
	[INSTR_BIT_SCHEDULER_SUBMIT] 	= INSTR_FLAG_SCHEDULER_SUBMIT,
	[INSTR_BIT_MEMORY] 				= INSTR_FLAG_MEMORY,
	[INSTR_BIT_API_BARRIER_WAIT] 	= INSTR_FLAG_API_BARRIER_WAIT,
	[INSTR_BIT_API_CREATE] 			= INSTR_FLAG_API_CREATE,
	[INSTR_BIT_API_DESTROY] 		= INSTR_FLAG_API_DESTROY,
	[INSTR_BIT_API_MUTEX_LOCK]		= INSTR_FLAG_API_MUTEX_LOCK,
	[INSTR_BIT_API_MUTEX_TRYLOCK]	= INSTR_FLAG_API_MUTEX_TRYLOCK,
	[INSTR_BIT_API_MUTEX_UNLOCK]	= INSTR_FLAG_API_MUTEX_UNLOCK,
	[INSTR_BIT_API_SUBMIT] 			= INSTR_FLAG_API_SUBMIT,
	[INSTR_BIT_API_PAUSE] 			= INSTR_FLAG_API_PAUSE,
	[INSTR_BIT_API_YIELD] 			= INSTR_FLAG_API_YIELD,
	[INSTR_BIT_API_WAITFOR] 		= INSTR_FLAG_API_WAITFOR,
	[INSTR_BIT_API_SCHEDPOINT] 		= INSTR_FLAG_API_SCHEDPOINT,
	[INSTR_BIT_API_ATTACH] 			= INSTR_FLAG_API_ATTACH,
	[INSTR_BIT_TASK] 				= INSTR_FLAG_TASK,
	[INSTR_BIT_KERNEL] 				= INSTR_FLAG_KERNEL,
	[INSTR_BIT_MAX] 				= ~(0ULL)
};

static const char *control_flags[] = {
	[INSTR_BIT_BASIC] 				= "basic",
	[INSTR_BIT_WORKER] 				= "worker",
	[INSTR_BIT_SCHEDULER] 			= "scheduler",
	[INSTR_BIT_SCHEDULER_SUBMIT] 	= "scheduler_submit",
	[INSTR_BIT_MEMORY] 				= "memory",
	[INSTR_BIT_API_BARRIER_WAIT] 	= "api_barrier_wait",
	[INSTR_BIT_API_CREATE] 			= "api_create",
	[INSTR_BIT_API_DESTROY] 		= "api_destroy",
	[INSTR_BIT_API_MUTEX_LOCK]		= "api_mutex_lock",
	[INSTR_BIT_API_MUTEX_TRYLOCK]	= "api_mutex_trylock",
	[INSTR_BIT_API_MUTEX_UNLOCK]	= "api_mutex_unlock",
	[INSTR_BIT_API_SUBMIT] 			= "api_submit",
	[INSTR_BIT_API_PAUSE] 			= "api_pause",
	[INSTR_BIT_API_YIELD] 			= "api_yield",
	[INSTR_BIT_API_WAITFOR] 		= "api_waitfor",
	[INSTR_BIT_API_SCHEDPOINT] 		= "api_schedpoint",
	[INSTR_BIT_API_ATTACH]	 		= "api_attach",
	[INSTR_BIT_TASK] 				= "task",
	[INSTR_BIT_KERNEL] 				= "kernel",
	[INSTR_BIT_MAX] 				= "all"
};

static inline void instr_update_control(const char *str) {
	const size_t total_strings = sizeof(control_flags) / sizeof(*control_flags);
	int neg = 0;
	uint64_t mask = 0;

	if (*str == '!') {
		neg = 1;
		str++;
	}

	for (int i = 0; i < total_strings; ++i) {
		assert(control_flags[i]);
		if (strcmp(str, control_flags[i]) == 0) {
			mask = control_flags_mask[i];
			break;
		}
	}

	if (!mask) {
		nosv_warn("Unknown instrumentation group '%s', ignoring", str);
	} else {
		if (neg)
			instr_ovni_control &= ~mask;
		else
			instr_ovni_control |= mask;
	}
}

void instr_parse_config(void)
{
	if (nosv_config.ovni_events.num_strings > 0) {
		// Fine-grained control
		for (int i = 0; i < nosv_config.ovni_events.num_strings; ++i) {
			const char *str = nosv_config.ovni_events.strings[i];
			instr_update_control(str);
		}
	} else {
		uint64_t level = nosv_config.ovni_level;
		if (level >= sizeof(control_levels) / sizeof(*control_levels)) {
			nosv_warn("ovni instrumentation level must be between 0 and 4. Defaulting to level 2");
			level = 2;
		}

		instr_ovni_control = control_levels[level];
	}
}
