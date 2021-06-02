/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#ifndef LIST_H
#define LIST_H

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
	((type *) ((void *) head) - offsetof(type, name))

static inline void list_init(list_head_t *head)
{
	head->next = NULL;
	head->prev = NULL;
}

// Add in the first position
static inline void list_add(list_head_t *head, list_head_t *n)
{
	n->next = head->next;
	head->next = n;
	n->prev = NULL;

	if (!head->prev)
		head->prev = n;
}

static inline list_head_t *list_pop_head(list_head_t *head)
{
	list_head_t *first = head->next;

	if (first) {
		head->next = first->next;

		// Was last element
		if (!head->next)
			head->prev = NULL;
	}

	return first;
}


/*
	Doubly-linked list with a count (C-List)
*/

typedef struct clist_head {
	struct list_head *next;
	struct list_head *prev;
	size_t cnt;
} clist_head_t;

static inline void clist_init(clist_head_t *head)
{
	head->cnt = 0;
	head->next = NULL;
	head->prev = NULL;
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
	n->next = head->next;
	head->next = n;
	n->prev = NULL;

	if (!head->prev)
		head->prev = n;

	head->cnt++;
}

#define clist_head(h) ((h)->next)

static inline list_head_t *clist_pop_head(clist_head_t *head)
{
	list_head_t *first = head->next;

	if (first) {
		head->next = first->next;

		// Was last element
		if (!head->next)
			head->prev = NULL;

		head->cnt--;
	}

	return first;
}

#endif // LIST_H