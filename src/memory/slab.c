/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>

#include "common.h"
#include "compiler.h"
#include "memory/backbone.h"
#include "memory/slab.h"

static inline void cpubucket_init(cpu_cache_bucket_t *cpubucket)
{
	cpubucket->slab = NULL;
	cpubucket->freelist = NULL;
}

static inline void cpubucket_setpage(cpu_cache_bucket_t *cpubucket, page_metadata_t *page)
{
	cpubucket->slab = (void *)page;
	cpubucket->freelist = page->freelist;
	page->freelist = NULL;
}

static inline int cpubucket_alloc(cpu_cache_bucket_t *cpubucket, void **obj)
{
	// The freelist is a linked list of blocks, which are in essence void * to the next block.
	// Here we advance the freelist and return the first element.
	if (cpubucket->freelist) {
		void *ret = cpubucket->freelist;
		void *next = *((void **)ret);
		cpubucket->freelist = next;

		*obj = ret;
		return 1;
	}

	return 0;
}

static inline int cpubucket_isinpage(cpu_cache_bucket_t *cpubucket, void *obj)
{
	if (cpubucket->slab) {
		uintptr_t uobj = (uintptr_t)obj;
		uintptr_t base = (uintptr_t)cpubucket->slab->addr;

		return (uobj >= base && uobj < (base + PAGE_SIZE));
	}

	return 0;
}

static inline void cpubucket_localfree(cpu_cache_bucket_t *cpubucket, void *obj)
{
	assert(cpubucket_isinpage(cpubucket, obj));

	*((void **)obj) = cpubucket->freelist;
	cpubucket->freelist = obj;
}

static inline size_t bucket_objinpage(cache_bucket_t *bucket)
{
	return PAGE_SIZE / bucket->obj_size;
}

// Initialize a page as a freelist of objects of a certain size.
static inline void bucket_initialize_page(cache_bucket_t *bucket, page_metadata_t *page)
{
	void **base = (void **)page->addr;
	size_t stride = bucket->obj_size / sizeof(void *);

	size_t obj_in_page = bucket_objinpage(bucket);
	for (size_t i = 0; i < obj_in_page; i++) {
		base[i * stride] = &base[(i + 1) * stride];
	}

	// Last
	base[(obj_in_page - 1) * stride] = NULL;
	page->freelist = page->addr;
	page->inuse_chunks = 0;
}

// Slow-path for allocation - not implemented
static inline void *bucket_allocate_slow(cache_bucket_t *bucket)
{
	return NULL;
}

static inline void bucket_refill_cpu_cache(cache_bucket_t *bucket, cpu_cache_bucket_t *cpubucket)
{
	size_t obj_in_page = bucket_objinpage(bucket);

	// Grab bucket lock
	nosv_spin_lock(&bucket->lock);

	// Find or allocate a free page
	if (!clist_empty(&bucket->partial)) {
		// Fast-path, we have a partial page
		list_head_t *firstpage = clist_pop_head(&bucket->partial);
		page_metadata_t *metadata = list_elem(firstpage, page_metadata_t, list_hook);

		size_t added = obj_in_page - metadata->inuse_chunks;
		metadata->inuse_chunks = obj_in_page;

		assert(added > 0);
		cpubucket_setpage(cpubucket, metadata);

		nosv_spin_unlock(&bucket->lock);
	} else if (!clist_empty(&bucket->free)) {
		// Fast-path as well, we have a cached free page
		list_head_t *firstpage = clist_pop_head(&bucket->free);
		page_metadata_t *metadata = list_elem(firstpage, page_metadata_t, list_hook);

		metadata->inuse_chunks = obj_in_page;
		cpubucket_setpage(cpubucket, metadata);

		nosv_spin_unlock(&bucket->lock);
	} else {
		// Slow-path, we need to allocate. Release the lock first
		nosv_spin_unlock(&bucket->lock);

		page_metadata_t *newpage = balloc();
		assert(newpage);

		// Initialize and add to cache
		bucket_initialize_page(bucket, newpage);
		newpage->inuse_chunks = obj_in_page;
		cpubucket_setpage(cpubucket, newpage);
	}
}

static inline void bucket_init(cache_bucket_t *bucket, size_t bucket_index)
{
	bucket->obj_size = (1ULL << bucket_index);
	nosv_spin_init(&bucket->lock);
	clist_init(&bucket->partial);
	clist_init(&bucket->free);

	for (int i = 0; i < NR_CPUS; ++i)
		cpubucket_init(&bucket->cpubuckets[i]);
}

static inline void *bucket_alloc(cache_bucket_t *bucket, int cpu)
{
	void *obj = NULL;

	assert(cpu < NR_CPUS);

	if (cpu >= 0) {
		cpu_cache_bucket_t *cpubucket = &bucket->cpubuckets[cpu];

		// Fast Path
		if (cpubucket_alloc(cpubucket, &obj))
			return obj;

		// Slower, there are no available chunks in the CPU cache and we have to refill
		bucket_refill_cpu_cache(bucket, cpubucket);

		__maybe_unused int ret = cpubucket_alloc(cpubucket, &obj);
		assert(ret);
		assert(obj);

		return obj;
	}

	// Slow-path
	return bucket_allocate_slow(bucket);
}

static inline void bucket_free(cache_bucket_t *bucket, void *obj, int cpu)
{
	cpu_cache_bucket_t *cpubucket = &bucket->cpubuckets[cpu];
	size_t obj_in_page = bucket_objinpage(bucket);

	assert(cpu >= 0);
	assert(cpu < NR_CPUS);
	if (cpubucket_isinpage(cpubucket, obj)) {
		// Fast path
		cpubucket_localfree(cpubucket, obj);
	} else {
		// Remote free, slow-path
		page_metadata_t *metadata =	page_metadata_from_block(obj);

		// Grab bucket lock
		nosv_spin_lock(&bucket->lock);

		// Post-decrement
		if (metadata->inuse_chunks-- < obj_in_page) {
			// Partial page, already in the partial list
			// Set "next"
			*((void **)obj) = metadata->freelist;
			metadata->freelist = obj;

			if (metadata->inuse_chunks == 0) {
				// We can put it in the free list or return it to the underlaying backbone
				// allocator. Lots of options
				// For now, we do NOTHING (stays in the partial list even full free)
			}
		} else {
			// Put in partial list
			// Set "next"
			*((void **)obj) = NULL;
			metadata->freelist = obj;

			// Add to partial list
			clist_add(&bucket->partial, &metadata->list_hook);
		}

		nosv_spin_unlock(&bucket->lock);
	}
}

void slab_init()
{
	for (size_t i = 0; i < SLAB_BUCKETS; ++i)
		bucket_init(&backbone_header->buckets[i], i + SLAB_ALLOC_MIN);
}

void *salloc(size_t size, int cpu)
{
	size_t allocsize = next_power_of_two(size);

	if (allocsize < SLAB_ALLOC_MIN)
		allocsize = SLAB_ALLOC_MIN;
	else if(allocsize >= SLAB_BUCKETS + SLAB_ALLOC_MIN)
		return NULL;

	cache_bucket_t *bucket = &backbone_header->buckets[allocsize - SLAB_ALLOC_MIN];

	return bucket_alloc(bucket, cpu);
}

void sfree(void *ptr, size_t size, int cpu)
{
	size_t allocsize = next_power_of_two(size);

	if (allocsize < SLAB_ALLOC_MIN)
		allocsize = SLAB_ALLOC_MIN;

	assert(allocsize < SLAB_BUCKETS + SLAB_ALLOC_MIN);

	cache_bucket_t *bucket = &backbone_header->buckets[allocsize - SLAB_ALLOC_MIN];

	bucket_free(bucket, ptr, cpu);
}