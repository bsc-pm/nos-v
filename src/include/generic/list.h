/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef LIST_H
#define LIST_H

#include <assert.h>
#include <stddef.h>

/*
	Vanilla C doubly-linked list.
	Embed a list_head_t in your structure for use.
	This looks pretty much like every generic C linked list.s
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

#define list_next_circular(p, h) \
	(((p)->next) ? ((p)->next) : ((h)->next))

static inline void list_init(list_head_t *head)
{
	head->next = NULL;
	head->prev = NULL;
}

// Add in the first position
static inline void list_add(list_head_t *head, list_head_t *n)
{
	n->next = head->next;
	if(head->next)
		head->next->prev = n;

	head->next = n;
	n->prev = NULL;

	if (!head->prev)
		head->prev = n;
}

// Add in the last position
static inline void list_add_tail(list_head_t *head, list_head_t *n)
{
	n->next = NULL;
	n->prev = head->prev;
	if (head->prev)
		head->prev->next = n;

	head->prev = n;

	if (!head->next)
		head->next = n;
}

static inline list_head_t *list_pop_head(list_head_t *head)
{
	list_head_t *first = head->next;

	if (first) {
		head->next = first->next;

		if (!head->next) {
			// Was last element
			head->prev = NULL;
		} else {
			head->next->prev = NULL;
		}
	}

	return first;
}

static inline list_head_t *list_front(list_head_t *head)
{
	return head->next;
}

static inline void list_remove(list_head_t *head, list_head_t *n)
{
	list_head_t *prev = n->prev;
	list_head_t *next = n->next;

	if (prev) {
		prev->next = next;
	} else {
		assert(head->next == n);
		head->next = next;
	}

	if (next) {
		next->prev = prev;
	} else {
		assert(head->prev == n);
		head->prev = prev;
	}

	n->next = NULL;
	n->prev = NULL;
}

/*
	Doubly-linked list with a count (C-List)
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
	list_remove(&head->__head, n);
	head->cnt--;
}

#define clist_head(h) ((h)->__head.next)

static inline list_head_t *clist_pop_head(clist_head_t *head)
{
	list_head_t *first = list_pop_head(&head->__head);

	if (first)
		head->cnt--;
	return first;
}

#endif // LIST_H
