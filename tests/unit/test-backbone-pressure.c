/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#include <stdlib.h>

#include <nosv/memory.h>

#include "memory/backbone.h"
#include "unit.h"

// 1GB memory for the backbone allocator
#define BACKBONE_STORAGE_SIZE (1 << 30)
static char backbone_storage[BACKBONE_STORAGE_SIZE];

static inline void initialize_backbone_fixture()
{
	backbone_alloc_init(backbone_storage, BACKBONE_STORAGE_SIZE, 1);
}

TEST_CASE(backbone_pressure_initial_in_bounds)
{
	initialize_backbone_fixture();

	float initialPressure = 0.0f;
	int ret = nosv_memory_get_pressure(&initialPressure);

	EXPECT_EQ(ret, NOSV_SUCCESS);
	// Initial pressure will be greater than 0 due to the metadata of the backbone allocator
	EXPECT_GT(initialPressure, 0.0f);
	EXPECT_LT(initialPressure, 1.0f);
}

TEST_CASE(backbone_pressure_increases)
{
	initialize_backbone_fixture();

	float initialPressure = 0.0f;
	float finalPressure = 0.0f;
	int ret = nosv_memory_get_pressure(&initialPressure);

	EXPECT_EQ(ret, NOSV_SUCCESS);

	for (int i = 0; i < 100; ++i) {
		void *alloc = balloc();
		EXPECT_NE(alloc, NULL);
	}

	ret = nosv_memory_get_pressure(&finalPressure);
	EXPECT_EQ(ret, NOSV_SUCCESS);

	EXPECT_GT(finalPressure, initialPressure);
	EXPECT_LT(finalPressure, 1.0f);
}

TEST_CASE(backbone_total_matches_configured)
{
	initialize_backbone_fixture();

	size_t size;
	int ret = nosv_memory_get_size(&size);

	EXPECT_EQ(ret, NOSV_SUCCESS);
	EXPECT_EQ(size, BACKBONE_STORAGE_SIZE);
}

TEST_CASE(backbone_initial_used_gt_zero)
{
	initialize_backbone_fixture();

	size_t used;
	int ret = nosv_memory_get_used(&used);

	EXPECT_EQ(ret, NOSV_SUCCESS);
	EXPECT_GT(used, 0);
}

#define fill_allocator()                                \
	{                                                   \
		size_t total, used;                             \
		int ret = nosv_memory_get_used(&used);          \
		EXPECT_EQ(ret, NOSV_SUCCESS);                   \
                                                        \
		ret = nosv_memory_get_size(&total);             \
		EXPECT_EQ(ret, NOSV_SUCCESS);                   \
                                                        \
		size_t num_blocks = (total - used) / PAGE_SIZE; \
		EXPECT_TRUE((total - used) % PAGE_SIZE == 0);   \
		EXPECT_GT(num_blocks, 0);                       \
                                                        \
		for (int i = 0; i < num_blocks; ++i) {          \
			void *alloc = balloc();                     \
			EXPECT_NE(alloc, NULL);                     \
		}                                               \
	}

TEST_CASE(backbone_pressure_reaches_1)
{
	initialize_backbone_fixture();
	fill_allocator();

	float pressure = 0.0f;
	int ret = nosv_memory_get_pressure(&pressure);
	EXPECT_EQ(ret, NOSV_SUCCESS);
	EXPECT_EQ(pressure, 1.0f);
}

TEST_CASE(backbone_fails_when_full)
{
	initialize_backbone_fixture();
	fill_allocator();

	void *ret = balloc();
	EXPECT_EQ(ret, NULL);
}

TEST_CASE(backbone_pressure_decreases)
{
	initialize_backbone_fixture();

	void **allocated_blocks = malloc(100 * sizeof(void *));

	for (int i = 0; i < 100; ++i) {
		allocated_blocks[i] = balloc();
		EXPECT_NE(allocated_blocks[i], NULL);
	}

	float initialPressure = 0.0f;
	float finalPressure = 0.0f;
	int ret = nosv_memory_get_pressure(&initialPressure);

	EXPECT_EQ(ret, NOSV_SUCCESS);

	for (int i = 0; i < 100; ++i) {
		bfree(allocated_blocks[i]);
	}

	ret = nosv_memory_get_pressure(&finalPressure);
	EXPECT_EQ(ret, NOSV_SUCCESS);

	EXPECT_LT(finalPressure, initialPressure);
	EXPECT_GT(finalPressure, 0.0f);
}