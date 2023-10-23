/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2023 Barcelona Supercomputing Center (BSC)
*/

#ifndef HT_H
#define HT_H

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>

#include "compiler.h"
#include "generic/hashfunction.h"

typedef uintptr_t hash_key_t;

typedef struct hash_entry {
	SLIST_ENTRY(hash_entry) list_hook;
	hash_key_t key;
	void *data;
} hash_entry_t;

SLIST_HEAD(hash_bucket_head, hash_entry);

typedef struct hash_bucket {
	struct hash_bucket_head head;
} hash_bucket_t;

typedef struct hash_table {
	int nbkt;
	hash_bucket_t free;
	hash_bucket_t *buckets;
} hash_table_t;


static inline size_t hash(hash_table_t *ht, hash_key_t key)
{
	assert(ht != NULL);
	assert(ht->nbkt > 0);
	assert((sizeof(hash_key_t) % sizeof(uint32_t)) == 0);
	size_t hash = (size_t) hashword((uint32_t *)&key, sizeof(hash_key_t)/sizeof(uint32_t), 0);
	return hash % ht->nbkt;
}

static inline int ht_init(hash_table_t *ht, size_t nbuckets, size_t nentries)
{
	size_t i;
	hash_entry_t *buffer;

	assert(ht != NULL);

	ht->buckets = (hash_bucket_t *)malloc(sizeof(hash_bucket_t) * nbuckets);
	if (!ht->buckets) {
		return 1;
	}

	ht->nbkt = nbuckets;
	SLIST_INIT(&ht->free.head);
	for (i = 0; i < nbuckets; i++) {
		SLIST_INIT(&ht->buckets[i].head);
	}

	if (nentries) {
		buffer = (hash_entry_t *)malloc(sizeof(hash_entry_t) * nentries);
		if (!buffer) {
			return 1;
		}

		for (i = 0; i < nentries; i++) {
			SLIST_INSERT_HEAD(&ht->free.head, &buffer[i], list_hook);
		}
	}

	return 0;
}

static inline hash_entry_t *get_free_entry(hash_table_t *ht)
{
	hash_entry_t *he;

	assert(ht != NULL);

	if (!SLIST_EMPTY(&ht->free.head)) {
		he = SLIST_FIRST(&ht->free.head);
		SLIST_REMOVE_HEAD(&ht->free.head, list_hook);
	} else {
		he = (hash_entry_t *) malloc(sizeof(hash_entry_t));
		// if error, we return null anyways
	}

	return he;
}

static inline int ht_insert(hash_table_t *ht, hash_key_t key, void *data)
{
	size_t i;
	hash_entry_t *he;
	hash_bucket_t *bkt;

	assert(ht != NULL);

	i = hash(ht, key);
	bkt = &ht->buckets[i];
	assert(bkt != NULL);

	he = get_free_entry(ht);
	if (!he)
		return 1;

	he->key = key;
	he->data = data;

	SLIST_INSERT_HEAD(&bkt->head, he, list_hook);

	return 0;
}

static inline void *ht_search(hash_table_t *ht, hash_key_t key)
{
	hash_entry_t *he;
	hash_bucket_t *bkt;
	size_t i;

	assert(ht != NULL);


	i = hash(ht, key);
	bkt = &ht->buckets[i];
	assert(bkt != NULL);

	SLIST_FOREACH(he, &bkt->head, list_hook) {
		if (he->key == key) {
			return he->data;
		}
	}

	return NULL;
}

static inline void *ht_pop(hash_table_t *ht)
{
	size_t i;
	hash_entry_t *he;
	hash_bucket_t *bkt;

	assert(ht != NULL);

	for (i = 0; i < ht->nbkt; i++) {
		bkt = &ht->buckets[i];
		assert(bkt != NULL);
		if (!SLIST_EMPTY(&bkt->head)) {
			break;
		}
	}

	if (i == ht->nbkt) {
		// no locks held if all buckets were traversed
		return NULL;
	}

	he = SLIST_FIRST(&bkt->head);
	SLIST_REMOVE_HEAD(&bkt->head, list_hook);

	SLIST_INSERT_HEAD(&ht->free.head, he, list_hook);

	return he->data;
}

// similar to the BSD SLIST_REMOVE_AFTER implementation
#define	SLIST_REMOVE_AFTER(elm, field) do {				\
	if ((elm)->field.sle_next) {					\
		(elm)->field.sle_next = (elm)->field.sle_next->field.sle_next;		\
	} else {							\
		(elm)->field.sle_next = NULL;				\
	}								\
} while (/*CONSTCOND*/0)

static inline void *ht_remove(hash_table_t *ht, hash_key_t key)
{
	hash_entry_t *he, *prev;
	hash_bucket_t *bkt;
	size_t i;

	assert(ht != NULL);

	i = hash(ht, key);
	bkt = &ht->buckets[i];
	assert(bkt != NULL);

	//printf("searching for key %lx in bin %u\n", key, i);

	// check if it's the first element in the list
	he = SLIST_FIRST(&bkt->head);
	if (he && he->key == key) {
		SLIST_REMOVE_HEAD(&bkt->head, list_hook);
		return he->data;
	}

	// now check all the other elements. It checks the first element again :<
	prev = NULL;
	SLIST_FOREACH(he, &bkt->head, list_hook) {
		if (he->key == key) {
			// middle or last element in the list
			assert(prev);
			SLIST_REMOVE_AFTER(prev, list_hook);
			return he->data;
		}
		prev = he;
	}

	return NULL;
}

#undef SLIST_REMOVE_AFTER

#endif //HT_H
