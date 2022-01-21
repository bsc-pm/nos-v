/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <stdatomic.h>

#include "common.h"
#include "compiler.h"
#include "generic/arch.h"
#include "instr.h"
#include "memory/asan.h"
#include "memory/backbone.h"
#include "memory/slab.h"

#define __SLAB_MAX_FREE_PAGES 16

static inline void cpubucket_init(cpu_cache_bucket_t *cpubucket)
{
	cpubucket->slab = NULL;
	cpubucket->freelist = NULL;
}

static inline void cpubucket_setpage(cpu_cache_bucket_t *cpubucket, page_metadata_t *page, void *freelist)
{
	cpubucket->slab = (void *)page;
	cpubucket->freelist = freelist;
}

static inline int page_metadata_cmpxchg_double(
	page_metadata_t *metadata,
	void *old_freelist, uint64_t old_inuse,
	void *new_freelist, uint64_t new_inuse)
{
#ifndef ARCH_HAS_DWCAS
	int ret = 0;
	nosv_spin_lock(&metadata->lock);
	if (metadata->freelist == old_freelist && metadata->inuse_chunks == old_inuse) {
		metadata->freelist = new_freelist;
		metadata->inuse_chunks = new_inuse;
		ret = 1;
	}
	nosv_spin_unlock(&metadata->lock);

	return ret;
#else
	return cmpxchg_double(&metadata->freelist, &metadata->inuse_chunks, old_freelist, old_inuse, new_freelist, new_inuse);
#endif
}

static inline int cpubucket_alloc(cpu_cache_bucket_t *cpubucket, void **obj, size_t size)
{
	// The freelist is a linked list of blocks, which are in essence void * to the next block.
	// Here we advance the freelist and return the first element.
	if (cpubucket->freelist) {
		void *ret = cpubucket->freelist;

		// Unpoison the chunk we're going to allocate
		asan_unpoison(ret, size);
		void *next = *((void **) ret);
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

static inline void cpubucket_localfree(cpu_cache_bucket_t *cpubucket, void *obj, size_t objsize)
{
	assert(cpubucket_isinpage(cpubucket, obj));

	*((void **)obj) = cpubucket->freelist;
	cpubucket->freelist = obj;

	// Poison again
	asan_poison(obj, objsize);
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
	// Temporarily unpoison the full page
	asan_unpoison(base, PAGE_SIZE);

	size_t obj_in_page = bucket_objinpage(bucket);
	for (size_t i = 0; i < obj_in_page; i++) {
		base[i * stride] = &base[(i + 1) * stride];
	}

	// Last
	base[(obj_in_page - 1) * stride] = NULL;
	page->freelist = page->addr;
	page->inuse_chunks = 0;

	// Poison everything again
	asan_poison(base, PAGE_SIZE);
}

static inline void bucket_refill_cpu_cache(cache_bucket_t *bucket, cpu_cache_bucket_t *cpubucket)
{
	size_t obj_in_page = bucket_objinpage(bucket);

	// Grab bucket lock
	nosv_spin_lock(&bucket->lock);

	// Search for a page
	page_metadata_t *metadata = NULL;
	void *freelist = NULL;

	if (!clist_empty(&bucket->partial)) {
		// Fast-path, we have a partial page
		list_head_t *firstpage = clist_pop_head(&bucket->partial);
		metadata = list_elem(firstpage, page_metadata_t, list_hook);

		uint64_t inuse = metadata->inuse_chunks;
		freelist = metadata->freelist;

		while (!page_metadata_cmpxchg_double(metadata, freelist, inuse, NULL, obj_in_page)) {
			inuse = metadata->inuse_chunks;
			freelist = metadata->freelist;
		}

		nosv_spin_unlock(&bucket->lock);
	} else if (!clist_empty(&bucket->free)) {
		// Fast-path as well, we have a cached free page
		list_head_t *firstpage = clist_pop_head(&bucket->free);
		nosv_spin_unlock(&bucket->lock);

		metadata = list_elem(firstpage, page_metadata_t, list_hook);
		// Nobody has allocated from here, so we should not be racing!
		metadata->inuse_chunks = obj_in_page;
		freelist = metadata->freelist;
		metadata->freelist = NULL;

		// All writes should be visible before any next one
		atomic_thread_fence(memory_order_release);
	} else {
		// Slow-path, we need to allocate. Release the lock first
		nosv_spin_unlock(&bucket->lock);

		metadata = balloc();
		assert(metadata);

		// Initialize and add to cache
		bucket_initialize_page(bucket, metadata);
		metadata->inuse_chunks = obj_in_page;
		freelist = metadata->freelist;
		metadata->freelist = NULL;

		// All writes should be visible before any next one
		atomic_thread_fence(memory_order_release);
	}

	assert(metadata);
	assert(freelist);
	cpubucket_setpage(cpubucket, metadata, freelist);
}

static inline void bucket_init(cache_bucket_t *bucket, size_t bucket_index)
{
	bucket->obj_size = (1ULL << bucket_index);
	nosv_spin_init(&bucket->lock);
	clist_init(&bucket->partial);
	clist_init(&bucket->free);

	for (int i = 0; i < NR_CPUS; ++i)
		cpubucket_init(&bucket->cpubuckets[i]);

	cpubucket_init(&bucket->slow_bucket);
	nosv_spin_init(&bucket->slow_bucket_lock);
}

static inline void *bucket_alloc(cache_bucket_t *bucket, int cpu, size_t original_size)
{
	void *obj = NULL;

	assert(cpu < NR_CPUS);
	assert(original_size <= bucket->obj_size);

	cpu_cache_bucket_t *cpubucket = &bucket->cpubuckets[cpu];

	// Maybe not that unlikely, but we _want_ this path to be slow so the fast-path can be fast
	if (unlikely(cpu < 0)) {
		cpubucket = &bucket->slow_bucket;
		// The slow-path is a cpubucket protected by a spinlock, usable for external threads
		// This substituted a more elaborate slow path that is vulnerable to ABA problems when using DWCAS.
		nosv_spin_lock(&bucket->slow_bucket_lock);
	}

	// Try to allocate from cached page
	if (cpubucket_alloc(cpubucket, &obj, original_size)) {
		if (unlikely(cpu < 0))
			nosv_spin_unlock(&bucket->slow_bucket_lock);

		return obj;
	}

	// Slower, there are no available chunks in the CPU cache and we have to refill
	bucket_refill_cpu_cache(bucket, cpubucket);

	__maybe_unused int ret = cpubucket_alloc(cpubucket, &obj, original_size);
	assert(ret);
	assert(obj);

	if (unlikely(cpu < 0))
		nosv_spin_unlock(&bucket->slow_bucket_lock);

	return obj;
}

static inline void bucket_free(cache_bucket_t *bucket, void *obj, int cpu)
{
	cpu_cache_bucket_t *cpubucket = &bucket->cpubuckets[cpu];
	size_t obj_in_page = bucket_objinpage(bucket);

	assert(cpu < NR_CPUS);
	if (cpu >= 0 && cpubucket_isinpage(cpubucket, obj)) {
		// Fast path
		cpubucket_localfree(cpubucket, obj, bucket->obj_size);
	} else {
		// Remote free, slow-path
		// This path is still very common, but it is fast in an uncontended page.
		page_metadata_t *metadata = page_metadata_from_block(obj);

		int success = 0;
		uint64_t inuse;

		do {
			inuse = metadata->inuse_chunks;
			void *next = metadata->freelist;
			*((void **)obj) = next;

			if (inuse == obj_in_page || inuse == 1) {
				// When inuse == 1, we speculatively grab the bucket lock to return the page, because
				// we may leave it totally unallocated
				// When inuse == obj_in_page, the page was full, so we have to add it to the partial
				// list (speculatively)
				nosv_spin_lock(&bucket->lock);
			}

			// Poison obj
			asan_poison(obj, bucket->obj_size);
			success = page_metadata_cmpxchg_double(metadata, next, inuse, obj, inuse - 1);

			if (!success && (inuse == obj_in_page || inuse == 1))
				nosv_spin_unlock(&bucket->lock);

			// Unpoison the range as we have failed to update it
			if (!success)
				asan_unpoison(obj, bucket->obj_size);

		} while (!success);

		if (inuse == 1) {
			// Was in the partial list, now it's fully free
			clist_remove(&bucket->partial, &metadata->list_hook);

			if (clist_count(&bucket->free) >= __SLAB_MAX_FREE_PAGES) {
				nosv_spin_unlock(&bucket->lock);
				bfree(metadata);
			} else {
				clist_add(&bucket->free, &metadata->list_hook);
				nosv_spin_unlock(&bucket->lock);
			}
		} else if (inuse == obj_in_page) {
			// Add to partial list
			clist_add(&bucket->partial, &metadata->list_hook);
			nosv_spin_unlock(&bucket->lock);
		}
	}
}

void slab_init(void)
{
	for (size_t i = 0; i < SLAB_BUCKETS; ++i)
		bucket_init(&backbone_header->buckets[i], i + SLAB_ALLOC_MIN);
}

void *salloc(size_t size, int cpu)
{
	void *ret;

	instr_salloc_enter();

	size_t allocsize = next_power_of_two(size);

	if (allocsize < SLAB_ALLOC_MIN) {
		allocsize = SLAB_ALLOC_MIN;
	} else if (allocsize >= SLAB_BUCKETS + SLAB_ALLOC_MIN) {
		ret = NULL;
		goto end;
	}

	cache_bucket_t *bucket = &backbone_header->buckets[allocsize - SLAB_ALLOC_MIN];

	ret = bucket_alloc(bucket, cpu, size);

end:
	instr_salloc_exit();

	return ret;
}

void sfree(void *ptr, size_t size, int cpu)
{
	instr_sfree_enter();

	size_t allocsize = next_power_of_two(size);

	if (allocsize < SLAB_ALLOC_MIN)
		allocsize = SLAB_ALLOC_MIN;

	assert(allocsize < SLAB_BUCKETS + SLAB_ALLOC_MIN);

	cache_bucket_t *bucket = &backbone_header->buckets[allocsize - SLAB_ALLOC_MIN];

	bucket_free(bucket, ptr, cpu);

	instr_sfree_exit();
}
