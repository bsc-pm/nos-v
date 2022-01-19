/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef PROC_H
#define PROC_H

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct process_identifier {
	unsigned long long start_time;
	pid_t pid;
} process_identifier_t;

static inline void parse_proc_stat(process_identifier_t *pi, FILE *fp)
{
	// Extracted from "man procfs"
	// We only get the first field, which is the PID, and then the start time, which is the 20th field
	// However, parsing proc stat is annoying because the second field is the name of the executable file,
	// enclosed by parenthesis "()", but the executable may have spaces and/or parenthesis as well, which
	// for some reason are not scaped.
	// So we *first* parse the PID and then seek the file past the last closing parenthesis.

	int ret = fscanf(fp, "%d", &pi->pid);
	assert(ret == 1);

	long pos = 0;
	rewind(fp);

	while (!feof(fp)) {
		char c = (char) fgetc(fp);
		if (c == ')')
			pos = ftell(fp);
	}

	// Now "pos" has the position of the last right bracket
	assert(pos > 0);
	ret = fseek(fp, pos + 1, SEEK_SET);
	assert(!ret);

	ret = fscanf(fp, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %llu",
		&pi->start_time);
	assert(ret == 1);
	assert(pi->start_time);
}

static inline process_identifier_t get_process(pid_t pid)
{
	char buf[256];
	process_identifier_t pi;
	pi.pid = -1;
	pi.start_time = 0;

	FILE *fp = NULL;

	if (pid) {
		sprintf(buf, "/proc/%d/stat", pid);
		fp = fopen(buf, "r");
	} else {
		fp = fopen("/proc/self/stat", "r");
	}

	// Note that fp being NULL is not a problem here, as it just means that the process we are inspecting died some time
	// ago. In that case, we just return pi.pid = -1.
	if (fp != NULL) {
		parse_proc_stat(&pi, fp);
		fclose(fp);
	}

	// If there was no file, pi.pid == -1
	return pi;
}

static inline process_identifier_t get_process_self(void)
{
	return get_process(0);
}

#endif // PROC_H
