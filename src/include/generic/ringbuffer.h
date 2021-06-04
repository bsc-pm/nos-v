/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <assert.h>
#include <stddef.h>
#include <string.h>

/*
	Typical ring buffer structure. We can define a custom "entry size", and elements are copied in and out of the buffer
	The user must provide its own buffer (that way we don't force it to be shared memory)
	Additionally, it uses the head and length implementation, which prevents having n-1 capacity
*/

typedef struct ring_buffer {
	size_t head;
	size_t length;
	size_t element_size;
	size_t total_elements;
	char *buffer;
} ring_buffer_t;

// Reference to a ring_buffer_t, size of the elements and number of elements
// Buffer has to be able to hold at least n * element_size bytes !!
static inline void ring_buffer_init(ring_buffer_t *rb, size_t element_size, size_t n, void *buffer)
{
	assert(rb);
	assert(element_size);
	assert(n);
	assert(buffer);

	rb->head = 0;
	rb->length = 0;
	rb->element_size = element_size;
	rb->total_elements = n;
	rb->buffer = (char *)buffer;
}

static inline int ring_buffer_full(ring_buffer_t *rb) {
	return (rb->length == rb->total_elements);
}

static inline int ring_buffer_push(ring_buffer_t *rb, void *elem)
{
	if (ring_buffer_full(rb))
		return 0;

	size_t next = (rb->head + rb->length) % rb->total_elements;
	memcpy(&rb->buffer[next * rb->element_size], elem, rb->element_size);
	rb->length++;

	return 1;
}

static inline int ring_buffer_empty(ring_buffer_t *rb)
{
	return (rb->length == 0);
}

// NULL if empty
static inline void *ring_buffer_front(ring_buffer_t *rb)
{
	if (ring_buffer_empty(rb))
		return NULL;

	return &rb->buffer[rb->head * rb->element_size];
}

// 0 if empty
static inline int ring_buffer_pop(ring_buffer_t *rb)
{
	if (ring_buffer_empty(rb))
		return 0;

	rb->head = (rb->head + 1) % rb->total_elements;
	rb->length--;

	return 1;
}

// 0 if empty
static inline int ring_buffer_pull(ring_buffer_t *rb, void *elem)
{
	void *front = ring_buffer_front(rb);
	if (!front)
		return 0;

	memcpy(elem, front, rb->element_size);
	ring_buffer_pop(rb);

	return 1;
}

#endif // RINGBUFFER_H