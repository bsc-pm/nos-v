/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPU_BITSET_H
#define CPU_BITSET_H

#include <assert.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>

#include "common.h"
#include "defaults.h"
#include "generic/bitset.h"

BITSET_DEFINE(_bitset, NR_CPUS)

typedef struct cpu_bitset {
	struct _bitset bits;
	int size;
} cpu_bitset_t;

static inline void cpu_bitset_init(cpu_bitset_t *bitset, int cpus)
{
	assert(cpus <= NR_CPUS);
	bitset->size = cpus;
	BIT_ZERO(cpus, &bitset->bits);
}

static inline int cpu_bitset_isset(const cpu_bitset_t *bitset, const int cpu)
{
	return BIT_ISSET(bitset->size, cpu, &bitset->bits);
}

static inline void cpu_bitset_set(cpu_bitset_t *bitset, const int cpu)
{
	BIT_SET(bitset->size, cpu, &bitset->bits);
}

static inline void cpu_bitset_clear(cpu_bitset_t *bitset, const int cpu)
{
	BIT_CLR(bitset->size, cpu, &bitset->bits);
}

static inline int cpu_bitset_count(const cpu_bitset_t *bitset)
{
	return BIT_COUNT(bitset->size, &bitset->bits);
}

// Returns the 0-index first set bit, -1 if bitset is empty
static inline int cpu_bitset_ffs(const cpu_bitset_t *bitset)
{
	return BIT_FFS(bitset->size, &bitset->bits) - 1;
}

// Returns the 0-index first set bit, -1 if no bits are greater than start_cpu are set
static inline int cpu_bitset_ffs_at(const cpu_bitset_t *bitset, const int start_cpu)
{
	assert(start_cpu < bitset->size);

	return BIT_FFS_AT(bitset->size, &bitset->bits, start_cpu + 1) - 1;
}

static inline int cpu_bitset_fls(const cpu_bitset_t *bitset)
{
	return BIT_FLS(bitset->size, &bitset->bits) - 1;
}

static inline void cpu_bitset_and(cpu_bitset_t *dest, const cpu_bitset_t *b)
{
	assert(dest->size == b->size);
	BIT_AND(dest->size, &dest->bits, &b->bits);
}

static inline void cpu_bitset_xor(cpu_bitset_t *dest, const cpu_bitset_t *b)
{
	assert(dest->size == b->size);
	BIT_XOR(dest->size, &dest->bits, &b->bits);
}

// Returns true if bitset a is different from bitset b
static inline bool cpu_bitset_cmp(const cpu_bitset_t *a, const cpu_bitset_t *b)
{
	assert(a->size == b->size);
	return BIT_CMP(a->size, &a->bits, &b->bits);
}

// Returns true if bitset a and b have any bits in common
static inline bool cpu_bitset_overlap(const cpu_bitset_t *a, const cpu_bitset_t *b)
{
	assert(a->size == b->size);
	return BIT_OVERLAP(a->size, &a->bits, &b->bits);
}

static inline void cpu_bitset_from_cpuset(cpu_bitset_t *dst, const cpu_set_t *src)
{
	assert(CPU_COUNT(src) <= NR_CPUS);
	cpu_bitset_init(dst, NR_CPUS);

	for (int cpu = 0; cpu < NR_CPUS; ++cpu) {
		if (CPU_ISSET(cpu, src)) {
			cpu_bitset_set(dst, cpu);
		}
	}
}

// Returns 0 if success, 1 otherwise.
// Parse a CPU set which is specified in separation by "-" and "," into a CPU set
// Additionally, allow for stride to be specified with ":", similarly to taskset
// This can parse both taskset-style lists and linux-style lists
static inline int cpu_bitset_parse_str(cpu_bitset_t *set, char *string_to_parse)
{
	int ret = 0;
	cpu_bitset_init(set, NR_CPUS);

	char *string_copy = strdup(string_to_parse);
	if (!string_copy)
		nosv_abort("Could not allocate memory");

	char *tok = strtok(string_to_parse, ",");
	while (tok) {
		int first_id, last_id, stride;
		int ret = sscanf(tok, "%d-%d:%d", &first_id, &last_id, &stride);

		switch (ret) {
			case 0:
				ret = 1;
				goto failed;
			case 1:
				last_id = first_id;
				fallthrough;
			case 2:
				stride = 1;
				break;
		}

		// Sanity check
		if (first_id > last_id) {
			ret = 1;
			goto failed;
		}

		for (int i = first_id; i <= last_id; i += stride)
			cpu_bitset_set(set, i);

		tok = strtok(NULL, ",");
	}

failed:
	free(string_copy);
	return ret;
}

static inline void cpu_bitset_print_mask(cpu_bitset_t *set)
{
	assert(set);
	char buffer[4096];

	int first_cpu, last_cpu;
	first_cpu = last_cpu = -1;
	char *curr = buffer;

	for (int i = 0; i < set->size; ++i) {
		if (cpu_bitset_isset(set, i)) {
			if (first_cpu == -1)
				first_cpu = i;
			last_cpu = i;
		} else {
			// End range
			if (first_cpu != -1) {
				if (first_cpu == last_cpu)
					curr += sprintf(curr, "%d,", first_cpu);
				else
					curr += sprintf(curr, "%d-%d,", first_cpu, last_cpu);
			}

			first_cpu = -1;
		}
	}

	if (first_cpu != -1) {
		if (first_cpu == last_cpu)
			curr += sprintf(curr, "%d,", first_cpu);
		else
			curr += sprintf(curr, "%d-%d,", first_cpu, last_cpu);
	}

	// IFF we have printed something (was not all zeros) remove trailing comma.
	if (last_cpu != -1)
		curr[-1] = '\0';
	else
		buffer[0] = '\0';

	nosv_warn("Effective binding: %s", buffer);
}

#define CPU_BITSET_FOREACH(bs, var) \
	for ((var) = cpu_bitset_ffs((bs)); (var) >= 0; (var) = (cpu_bitset_ffs_at((bs), (var))))

static inline void cpu_bitset_to_cpuset(cpu_set_t *dst, cpu_bitset_t *src)
{
	CPU_ZERO(dst);

	int cpu;
	CPU_BITSET_FOREACH (src, cpu) {
		CPU_SET(cpu, dst);
	}
}


#endif // CPU_BITSET_H
