/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2022 Barcelona Supercomputing Center (BSC)
*/

#include <stdlib.h>

#include "generic/heap.h"
#include "unit.h"

struct heap_test_elem {
	int elem;
	heap_node_t heap_hook;
};

static inline int min_heap_cmp(heap_node_t *a, heap_node_t *b)
{
	struct heap_test_elem *el_a = heap_elem(a, struct heap_test_elem, heap_hook);
	struct heap_test_elem *el_b = heap_elem(b, struct heap_test_elem, heap_hook);

	return (el_a->elem < el_b->elem) - (el_b->elem < el_a->elem);
}

static inline int max_heap_cmp(heap_node_t *a, heap_node_t *b)
{
	struct heap_test_elem *el_a = heap_elem(a, struct heap_test_elem, heap_hook);
	struct heap_test_elem *el_b = heap_elem(b, struct heap_test_elem, heap_hook);

	// Comparing two int fields
	// First, we check c = (a > b), which will be either 1 or 0
	// Then, we do d = (b > a), either 1 or 0
	// If we then calculate e = c - d, e will be 1 if a > b, -1 if b > a, or 0 if a = b.
	return (el_a->elem > el_b->elem) - (el_b->elem > el_a->elem);
}

TEST_CASE(heap_can_insert_one_element) {
	struct heap_test_elem elem;
	heap_head_t heap;
	heap_init(&heap);
	elem.elem = 0;

	heap_insert(&heap, &elem.heap_hook, min_heap_cmp);
	heap_node_t *ret = heap_pop_max(&heap, min_heap_cmp);

	EXPECT_TRUE(ret);

	EXPECT_EQ(ret, &elem.heap_hook);
}

TEST_CASE(heap_cannot_pull_empty) {
	heap_head_t heap;
	heap_init(&heap);

	heap_node_t *ret = heap_pop_max(&heap, min_heap_cmp);

	EXPECT_FALSE(heap_pop_max(&heap, min_heap_cmp));
}

TEST_CASE(heap_respect_min) {
	heap_head_t heap;
	heap_init(&heap);

	struct heap_test_elem elems[2];
	elems[0].elem = 0;
	elems[1].elem = 1;

	heap_insert(&heap, &elems[0].heap_hook, min_heap_cmp);
	heap_insert(&heap, &elems[1].heap_hook, min_heap_cmp);

	EXPECT_EQ(heap_pop_max(&heap, min_heap_cmp), &elems[0].heap_hook);
	EXPECT_EQ(heap_pop_max(&heap, min_heap_cmp), &elems[1].heap_hook);

	heap_insert(&heap, &elems[1].heap_hook, min_heap_cmp);
	heap_insert(&heap, &elems[0].heap_hook, min_heap_cmp);

	EXPECT_EQ(heap_pop_max(&heap, min_heap_cmp), &elems[0].heap_hook);
	EXPECT_EQ(heap_pop_max(&heap, min_heap_cmp), &elems[1].heap_hook);
}

TEST_CASE(heap_respect_max) {
	heap_head_t heap;
	heap_init(&heap);

	struct heap_test_elem elems[2];
	elems[0].elem = 0;
	elems[1].elem = 1;

	heap_insert(&heap, &elems[0].heap_hook, max_heap_cmp);
	heap_insert(&heap, &elems[1].heap_hook, max_heap_cmp);

	EXPECT_EQ(heap_pop_max(&heap, max_heap_cmp), &elems[1].heap_hook);
	EXPECT_EQ(heap_pop_max(&heap, max_heap_cmp), &elems[0].heap_hook);

	heap_insert(&heap, &elems[1].heap_hook, max_heap_cmp);
	heap_insert(&heap, &elems[0].heap_hook, max_heap_cmp);

	EXPECT_EQ(heap_pop_max(&heap, max_heap_cmp), &elems[1].heap_hook);
	EXPECT_EQ(heap_pop_max(&heap, max_heap_cmp), &elems[0].heap_hook);
}
