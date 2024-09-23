/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <numa.h>
#include <semaphore.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <nosv.h>
#include <nosv/affinity.h>

#include "test.h"
#include "common/utils.h"

// getcpu is only included in GLIBC since 2.29
#if !__GLIBC_PREREQ(2, 29)

// Fix for very old Linux versions
#ifndef SYS_getcpu
#define SYS_getcpu 0
#define SKIP_CPU_TEST 1
#endif

static inline int getcpu(unsigned int *cpu, unsigned int *node)
{
	return syscall(SYS_getcpu, cpu, node);
}
#endif

test_t t;

volatile unsigned int test = 0;

// Get number of NUMA nodes with libnuma
int libnuma_get_numa_nodes(void);
// Get array of NUMA nodes with libnuma
int *libnuma_get_numa_nodes_array(void);
// Search val in arr
int search_in_arr(int *arr, int size, int val);
// Test all values in arr exist in nosv_arr
int test_arr(test_t *t, int *arr, int size, int *nosv_arr, int nosv_size, char *type_str);

int main(void)
{
	// Number of available CPUs
	int num_cpus = get_cpus();
	// Array containing all the system CPU ids
	int *cpu_sys_ids = get_cpu_array();
	// Number of available NUMAs
	int num_numas = libnuma_get_numa_nodes();
	// Array containing all the system NUMA ids
	int *numa_sys_ids = libnuma_get_numa_nodes_array();

	int ntests = 0;
	#ifndef SKIP_CPU_TEST
		ntests += 1 + num_cpus;
	#endif
	if (numa_available() >= 0)
		ntests += 1 + num_numas;
	test_init(&t, ntests);


	CHECK(nosv_init());

	#ifndef SKIP_CPU_TEST
		test_check(&t, num_cpus == nosv_get_num_cpus(), "Number of CPUs returned by nOS-V is equal to glibc's");
	#endif

	if (numa_available() >= 0)
		test_check(&t, num_numas == nosv_get_num_numa_nodes(), "Number of NUMA nodes returned by nOS-V is equal to glibc's");


	// Check that all CPUs found in this process' mask are seen by nOS-V
	int nosv_num_cpus = nosv_get_num_cpus();
	int *nosv_cpus = nosv_get_available_cpus();
	test_arr(&t, cpu_sys_ids, num_cpus, nosv_cpus, nosv_num_cpus, "CPU");

	// Check that all NUMA nodes found by libnumam in this process are seen by nOS-V
	int nosv_num_numas = nosv_get_num_numa_nodes();
	int *nosv_numas = nosv_get_available_numa_nodes();
	test_arr(&t, numa_sys_ids, num_numas, nosv_numas, nosv_num_numas, "NUMA node");

	free(cpu_sys_ids);
	free(numa_sys_ids);
	free(nosv_cpus);
	free(nosv_numas);

	CHECK(nosv_shutdown());

	test_end(&t);
	return 0;
}


int libnuma_get_numa_nodes(void)
{
	return numa_num_task_nodes();
}

int *libnuma_get_numa_nodes_array(void)
{
	int *numa_array = malloc(sizeof(int) * libnuma_get_numa_nodes());
	int i = 0;
	for (int numa = 0; numa < numa_num_configured_nodes(); ++numa) {
		if (numa_bitmask_isbitset(numa_all_nodes_ptr, numa))
			numa_array[i++] = numa;
	}
	assert(i == libnuma_get_numa_nodes());
	return numa_array;
}

int search_in_arr(int *arr, int size, int val)
{
	for (int i = 0; i < size; ++i) {
		if (arr[i] == val)
			return 1;
	}
	return 0;
}

int test_arr(test_t *t, int *arr, int size, int *nosv_arr, int nosv_size, char *type_str)
{
	for (int i = 0; i < size; ++i) {
		int sid = arr[i];
		test_check(t, search_in_arr(nosv_arr, nosv_size, sid), "%s %d is available in nOS-V", type_str, sid);
	}
}
