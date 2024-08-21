/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)
*/

#ifndef LIST_H
#define LIST_H

#include <assert.h>
#include <stddef.h>

#include "compiler.h"

/*
	Vanilla C doubly-linked list.
	Embed a list_head_t in your structure for use.

	This looks pretty much like every generic C linked list.
	Every list is a circular list, so every head is initialized
	so that it's next and prev point to itself. You can iterate
	the list by checking that next is not equal to head. We
	provide a couple of macros for such operations at the end
	of the file.
*/

typedef struct list_head {
	struct list_head *next;
	struct list_head *prev;
} list_head_t;

/*
	head - pointer to the embedded list_head_t
	type - name of the type the list_head_t is embedded in
	name - name of the list_head_t member
*/
#define list_elem(head, type, name) \
	((type *) (((char *) (head)) - offsetof(type, name)))

#define list_next(p) \
	((p)->next)

// Gets next circular node ignoring head (h)
#define list_next_circular(p, h) \
	(list_is_head((p)->next, (h)) ? ((h)->next) : ((p)->next))



static inline int list_is_head(const struct list_head *n, const struct list_head *head)
{
	return n == head;
}

static inline int list_empty(list_head_t *head)
{
	return list_is_head(head->next, head);
}

static inline int list_node_has_list(list_head_t *n)
{
	if (!n->next) {
		// Pertains to no queue
		assert(!n->prev);
		return 0;
	} else if (list_is_head(n->next, n)) {
    	// Is head, and has no elements. Can happen when tasks in priority queue
		assert(n == n->prev);
		return 0;
	} else { // Pertains to a queue
		// n->next != NULL
		// n->next != n
		assert(n->prev);
		// assert we do not point to ourselves with prev
		assert(!list_is_head(n->prev, n));
		return 1;
	}
}

static inline void list_init(list_head_t *head)
{
	head->next = head;
	head->prev = head;
}

static inline void __list_add(list_head_t *prev, list_head_t *n, list_head_t *next)
{
	assert(prev->prev);
	assert(next->next);
	next->prev = n;
	prev->next = n;
	n->next = next;
	n->prev = prev;
}

// Add after head
static inline void list_add(list_head_t *head, list_head_t *n)
{
	assert(!list_node_has_list(n));
	assert(head->next && head->prev);
	assert(n->next && n->prev);
	__list_add(head, n, head->next);
}

// Add before head
static inline void list_add_tail(list_head_t *head, list_head_t *n)
{
	assert(!list_node_has_list(n));
	assert(head->next && head->prev);
	__list_add(head->prev, n, head);
}

// Remove entry by making prev and next point to each other
//
// For internal list manipulation, where we already know
// the next and prev pointers.
static inline void __list_remove(list_head_t *prev, list_head_t *next)
{
	next->prev = prev;
	prev->next = next;
}

// Removes the front of the list (head->next). If only head in list, return NULL.
static inline list_head_t *list_pop_front(list_head_t *head)
{
	assert(head->next && head->prev);
	if (list_empty(head)) // No values in list, only head
		return NULL;

	list_head_t *old_next = list_next(head); // the one to remove
	assert(list_node_has_list(old_next));

	__list_remove(head, old_next->next);

	list_init(old_next);

	return old_next;
}

// Inserts new in old's place. New cannot be already a member of old's list
static inline void list_replace(list_head_t *old, list_head_t *new)
{
	assert(old->next && old->prev);
	if (old == old->next) { // Empty list, only head
		list_init(new);
	} else {
		assert(list_node_has_list(old));

		// Init new
		new->next = old->next;
		new->prev = old->prev;

		// Update neighbours
		new->next->prev = new;
		new->prev->next = new;

		// Reset old
		list_init(old);
	}
}

// Returns the next element, and NULL if only head is in list
static inline list_head_t *list_front(list_head_t *head)
{
	assert(head->next && head->prev);

	if (list_empty(head))
		return NULL; // signal that this list only has a head with no other members
	return head->next;
}

static inline void list_remove(list_head_t *n)
{
	assert(n->next);
	assert(n->prev);
	// Make sure its pointers do not point to itself
	assert(!list_is_head(n->next, n));
	assert(!list_is_head(n->prev, n));

	__list_remove(n->prev, n->next);

	list_init(n);
}

/*
	Doubly-linked list with a count (C-list)
*/

typedef struct clist_head {
	list_head_t __head;
	size_t cnt;
} clist_head_t;

static inline void clist_init(clist_head_t *head)
{
	head->cnt = 0;
	list_init(&head->__head);
}

static inline size_t clist_count(clist_head_t *head)
{
	return head->cnt;
}

static inline int clist_empty(clist_head_t *head)
{
	return (head->cnt == 0);
}

// Add in the first position
static inline void clist_add(clist_head_t *head, list_head_t *n)
{
	list_add(&head->__head, n);
	head->cnt++;
}

static inline void clist_remove(clist_head_t *head, list_head_t *n)
{
	list_remove(n);
	head->cnt--;
}

#define clist_head(h) ((h)->__head.next)

static inline int clist_is_head(const struct list_head *n, const struct clist_head *clist)
{
	assert(n);
	assert(clist);
	return n == clist_head(clist);
}

static inline list_head_t *clist_pop_front(clist_head_t *head)
{
	list_head_t *first = list_pop_front(&head->__head);

	if (first)
		head->cnt--;
	return first;
}

// Traverse all nodes in list excluding head
#define list_for_each(pos, head) \
	for (pos = list_next(head); !list_is_head(pos, (head)); pos = list_next(pos))

#define list_for_each_start_at(pos, head) \
	for (; !list_is_head(pos, (head)); pos = list_next(pos))

#define list_for_each_pop(pos, head) \
	for (pos = list_pop_front(head); pos; pos = list_pop_front(head))

#define clist_for_each_pop(pos, head) \
	for (pos = clist_pop_front(head); pos; pos = clist_pop_front(head))

#endif // LIST_H
