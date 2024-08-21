/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef CPU_BITSET_H
#define CPU_BITSET_H

#include <assert.h>
#include <stdio.h>

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

static inline int cpu_bitset_ffs(const cpu_bitset_t *bitset)
{
	return BIT_FFS(bitset->size, &bitset->bits) - 1;
}

static inline int cpu_bitset_ffs_at(const cpu_bitset_t *bitset, const int cpu)
{
	if (bitset->size <= cpu)
		return -1;

	return BIT_FFS_AT(bitset->size, &bitset->bits, cpu + 1) - 1;
}

static inline int cpu_bitset_fls(const cpu_bitset_t *bitset)
{
	return BIT_FLS(bitset->size, &bitset->bits) - 1;
}

// Returns true if bitset a is different from bitset b
static inline int cpu_bitset_cmp(const cpu_bitset_t *a, const cpu_bitset_t *b)
{
	assert(a->size == b->size);
	return BIT_CMP(a->size, &a->bits, &b->bits);
}

#define CPU_BITSET_FOREACH(bs, var) \
	for ((var) = cpu_bitset_ffs((bs)); (var) >= 0; (var) = (cpu_bitset_ffs_at((bs), (var))))

#endif // CPU_BITSET_H
