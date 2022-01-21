/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2022 Barcelona Supercomputing Center (BSC)
*/

#include <stdlib.h>

#include "generic/ringbuffer.h"
#include "unit.h"

// Sample unit test case
// Use common functions to initialize your structures
ring_buffer_t *init_rb(int el_size, int n)
{
	ring_buffer_t *rb = malloc(sizeof(ring_buffer_t));
	assert(rb);

	char *buffer = malloc(n * el_size);
	assert(rb);

	ring_buffer_init(rb, el_size, n, buffer);

	return rb;
}

void destroy_rb(ring_buffer_t *rb)
{
	free(rb->buffer);
	free(rb);
}

// Use descriptive names for your tests
// Test one logical thing at a time
// Exercise edge cases
TEST_CASE(ringbuffer_can_push_pull_one_element) {
	ring_buffer_t *rb = init_rb(sizeof(int), 1);

	int element = 3;
	EXPECT_TRUE(ring_buffer_push(rb, &element));

	int element_ret;
	EXPECT_TRUE(ring_buffer_pull(rb, &element_ret));

	EXPECT_EQ(element, element_ret);

	destroy_rb(rb);
}

TEST_CASE(ringbuffer_cannot_pull_empty) {
	ring_buffer_t *rb = init_rb(sizeof(int), 16);

	int element = 0xBAADBEEF;
	EXPECT_FALSE(ring_buffer_pull(rb, &element));

	EXPECT_EQ(element, 0xBAADBEEF);

	destroy_rb(rb);
}

TEST_CASE(ringbuffer_can_see_full) {
	ring_buffer_t *rb = init_rb(sizeof(int), 2);
	int element = 0;

	EXPECT_FALSE(ring_buffer_full(rb));
	EXPECT_TRUE(ring_buffer_push(rb, &element));
	EXPECT_FALSE(ring_buffer_full(rb));
	EXPECT_TRUE(ring_buffer_push(rb, &element));
	EXPECT_TRUE(ring_buffer_full(rb));
	EXPECT_TRUE(ring_buffer_pull(rb, &element));
	EXPECT_FALSE(ring_buffer_full(rb));

	destroy_rb(rb);
}

TEST_CASE(ringbuffer_can_see_empty) {
	ring_buffer_t *rb = init_rb(sizeof(int), 2);
	int element = 0;

	EXPECT_TRUE(ring_buffer_empty(rb));
	EXPECT_TRUE(ring_buffer_push(rb, &element));
	EXPECT_FALSE(ring_buffer_empty(rb));
	EXPECT_TRUE(ring_buffer_pull(rb, &element));
	EXPECT_TRUE(ring_buffer_empty(rb));

	destroy_rb(rb);
}

TEST_CASE(ringbuffer_cannot_push_full) {
	ring_buffer_t *rb = init_rb(sizeof(int), 1);
	int element = 1;

	EXPECT_TRUE(ring_buffer_push(rb, &element));

	element = 2;
	EXPECT_FALSE(ring_buffer_push(rb, &element));
	EXPECT_TRUE(ring_buffer_pull(rb, &element));
	EXPECT_EQ(element, 1);

	destroy_rb(rb);
}
