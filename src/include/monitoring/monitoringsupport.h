/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef MONITORINGSUPPORT_H
#define MONITORINGSUPPORT_H

#define DEFAULT_COST 1
#define PREDICTION_UNAVAILABLE -1.0

enum monitoring_status_t {
	// The task is ready to be executed
	ready_status = 0,
	// The task is being executed
	executing_status,
	// The task is blocked
	paused_status,
	num_status,
	null_status = -1
};

#endif // MONITORINGSUPPORT_H
