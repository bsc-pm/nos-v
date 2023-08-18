/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
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
	INSTR_FLAG_BASIC,
	INSTR_FLAG_WORKER,
	INSTR_FLAG_SCHEDULER,
	INSTR_FLAG_SCHEDULER_SUBMIT,
	INSTR_FLAG_ALLOC,
	INSTR_FLAG_API_SUBMIT,
	INSTR_FLAG_API_PAUSE,
	INSTR_FLAG_API_YIELD,
	INSTR_FLAG_API_WAITFOR,
	INSTR_FLAG_API_SCHEDPOINT,
	INSTR_FLAG_TASK,
	INSTR_FLAG_KERNEL,
	~(0ULL)
};

// Keep the same order as the flags here
static const char *control_flags[] = {
	"basic",
	"worker",
	"scheduler",
	"scheduler_submit",
	"alloc",
	"api_submit",
	"api_pause",
	"api_yield",
	"api_waitfor",
	"api_schedpoint",
	"task",
	"kernel",
	"all"
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
			nosv_warn("ovni instrumentation level must be between 0 and 4. Defaulting to level 0");
			level = 0;
		}

		instr_ovni_control = control_levels[level];
	}
}
