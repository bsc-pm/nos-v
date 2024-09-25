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
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <nosv.h>
#include <nosv/affinity.h>
#include <nosv/hwinfo.h>

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

// Get array of NUMA nodes with libnuma, and return size
int libnuma_get_numa_nodes_array(int *cpus_enabled, int size, int **out_numanodes);
// Search val in arr
int search_in_arr(int *arr, int size, int val);
// Test all values in arr exist in nosv_arr
void test_arr(test_t *t, int *arr, int size, int *nosv_arr, int nosv_size, char *type_str);

int main(void)
{
	// Number of available CPUs
	int num_cpus = 0;
	// Array containing all the system CPU ids
	int *cpu_sys_ids = NULL;
	// Number of available NUMAs
	int *numa_sys_ids = NULL;
	int num_numas = 0;

	int ntests = 0;
	#ifndef SKIP_CPU_TEST
		num_cpus = get_cpus();
		cpu_sys_ids = get_cpu_array();;
		ntests += 1 + num_cpus;
	if (numa_available() != -1) {
		num_numas = libnuma_get_numa_nodes_array(cpu_sys_ids, num_cpus, &numa_sys_ids);
		ntests += 1 + num_numas;
	}
	#endif
	test_init(&t, ntests);


	CHECK(nosv_init());

	#ifndef SKIP_CPU_TEST
		int nosv_num_cpus = nosv_get_num_cpus();
		int *nosv_cpus = nosv_get_available_cpus();

		// Check that all CPUs found in this process' mask are seen by nOS-V
		test_check(&t, num_cpus == nosv_get_num_cpus(), "Number of CPUs returned by nOS-V (%d) is equal to glibc's (%d).", nosv_num_cpus, num_cpus);
		test_arr(&t, cpu_sys_ids, num_cpus, nosv_cpus, nosv_num_cpus, "CPU");
		free(cpu_sys_ids);
		free(nosv_cpus);

	if (numa_available() != -1) {
		int nosv_num_numas = nosv_get_num_numa_nodes();
		int *nosv_numas = nosv_get_available_numa_nodes();

		// Check that all NUMA nodes found by libnumam in this process are seen by nOS-V
		test_check(&t, num_numas == nosv_get_num_numa_nodes(), "Number of NUMA nodes returned by nOS-V (%d) is equal to the ones where we have enabled CPUs (%d).", nosv_num_numas, num_numas);
		test_arr(&t, numa_sys_ids, num_numas, nosv_numas, nosv_num_numas, "NUMA node");
		free(numa_sys_ids);
		free(nosv_numas);
	}
	#endif

	CHECK(nosv_shutdown());

	test_end(&t);
	return 0;
}


int libnuma_get_numa_nodes_array(int *cpus_enabled, int size, int **out_numanodes)
{
	struct bitmask *node_bm = numa_allocate_nodemask();
	if (!node_bm)
		return -1;

	// Traverse all CPUs in cpus_enabled and ask for which NUMA they belong to
	// These may be a subset of all NUMA nodes enabled, but that is the NUMA nOS-V recognizes
	for (int i = 0; i < size; ++i) {
		int numa = numa_node_of_cpu(cpus_enabled[i]);
		assert(numa >= 0);
		numa_bitmask_setbit(node_bm, numa);
	}
	int numa_nodes_size = numa_bitmask_weight(node_bm);

	// Now, traverse the bitmask and collect all the NUMA nodes in an array
	*out_numanodes = malloc((numa_nodes_size) * sizeof(int));
	int idx = 0;
	for (int i = 0; i < numa_num_possible_nodes(); ++i) {
		if (numa_bitmask_isbitset(node_bm, i)) {
			(*out_numanodes)[idx++] = i;
		}
	}
	assert(idx = numa_nodes_size);
	numa_free_nodemask(node_bm);

	return numa_nodes_size;
}

int search_in_arr(int *arr, int size, int val)
{
	for (int i = 0; i < size; ++i) {
		if (arr[i] == val)
			return 1;
	}
	return 0;
}

void test_arr(test_t *t, int *arr, int size, int *nosv_arr, int nosv_size, char *type_str)
{
	for (int i = 0; i < size; ++i) {
		int sid = arr[i];
		test_check(t, search_in_arr(nosv_arr, nosv_size, sid), "%s %d is available in nOS-V", type_str, sid);
	}
}
