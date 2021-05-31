/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include "memory/backbone.h"
#include "memory/slab.h"

void cpubucket_init(cpu_cache_bucket_t *cpubucket)
{
		// _cachedObjs._list = nullptr;
		// _cachedObjs._n = 0;
		// _freelist = nullptr;

}

void cpubucket_setpage(cpu_cache_bucket_t *cpubucket, page_metadata_t *page)
{
		// _cachedObjs._list = page;
		// _cachedObjs._n = cnt;
		// _freelist = page->freelist;
		// page->freelist = nullptr;

}

int cpubucket_alloc(cpu_cache_bucket_t *cpubucket, void **obj)
{
		// // We use the count in the list as the other refers to inUse objects,
		// // Which are all while the list is in the cpu cache
		// if (_cachedObjs._n > 0) {
		// 	obj = _freelist;

		// 	void *next = *((void **)obj);
		// 	_freelist = next;
		// 	_cachedObjs._n--;

		// 	return true;
		// }

		// return false;

}

int cpubucket_isinpage(cpu_cache_bucket_t *cpubucket, void *obj)
{
		// if (_cachedObjs._list == nullptr) {
		// 	return false;
		// } else {
		// 	uintptr_t uobj = (uintptr_t)obj;
		// 	uintptr_t base = (uintptr_t)_cachedObjs._list->addr;

		// 	return (uobj >= base && uobj < (base + LocalAllocator::chunkSize));
		// }

}

int cpubucket_localfree(cpu_cache_bucket_t *cpubucket, void *obj)
{
		// assert(isInPage(obj));

		// *((void **) obj) = _freelist;
		// _freelist = obj;
		// _cachedObjs._n++;

}

int bucket_objinpage(cache_bucket_t *bucket)
{
		// return LocalAllocator::chunkSize / _objSize;
}

void bucket_initialize_page(cache_bucket_t *bucket, page_metadata_t *page)
{
		// void **pageData = (void **)page->addr;
		// size_t stride = _objSize / sizeof(void *);

		// for (size_t i = 0; i < objInPage(); i++) {
		// 	pageData[i * stride] = &pageData[(i+1) * stride];
		// }

		// // Last
		// pageData[(objInPage() - 1) * stride] = nullptr;
		// page->freelist = page->addr;
		// page->inUse = 0;

}

// Slow-path for allocation - not implemented
void *bucket_allocate_slow()
{
	return NULL;
}

void bucket_refill_cpu_cache(cache_bucket_t *bucket, cpu_cache_bucket_t* cpubucket)
{
		// // Get local allocator
		// LocalAllocator *alloc = Hive::getSharedMemory().getLocalAllocator();

		// // Grab bucket lock
		// _lock.lock();

		// // Find or allocate a free page
		// if (_partial._n > 0) {
		// 	// Fast-path, we have a partial page
		// 	size_t addedObjects = objInPage() - _partial._list->inUse;
		// 	_partial._list->inUse = objInPage();
		// 	assert(addedObjects > 0);
		// 	bucket.setPage(_partial._list, addedObjects);

		// 	// Remove first page from partial list
		// 	_partial._list = _partial._list->next;
		// 	_partial._n--;
		// 	// Non-circular?
		// 	// _partial._list->prev = nullptr;
		// 	_lock.unlock();
		// } else if (_free._n > 0) {
		// 	// Fast-path as well, we have a cached free page
		// 	_free._list->inUse = objInPage();
		// 	bucket.setPage(_free._list, objInPage());

		// 	// Remove from free list
		// 	_free._list = _free._list->next;
		// 	_free._n--;
		// 	// Non-circular?
		// 	// _free._list->prev = nullptr;
		// 	_lock.unlock();
		// } else {
		// 	// Slow-path, we need to allocate
		// 	_lock.unlock();
		// 	LocalAllocator::PageMetadata *newPage = alloc->getChunk(-1);
		// 	assert(newPage != nullptr);

		// 	// Initialize and add to cache
		// 	initializePage(newPage);
		// 	newPage->inUse = objInPage();
		// 	bucket.setPage(newPage, objInPage());
		// }

}

void bucket_init(cache_bucket_t *bucket, size_t bucket_index)
{
		// _objSize = (1ULL << bucketIndex);
}

void *bucket_alloc(cache_bucket_t *bucket, int cpu)
{
		// void *obj = nullptr;

		// if (cpu != -1) {
		// 	// Fast-path
		// 	if (_cpuCacheBuckets[cpu].alloc(obj))
		// 		return obj;

		// 	// There is no space in the CPU cache, re-fill
		// 	// Slow-path
		// 	refillCPUCache(_cpuCacheBuckets[cpu]);

		// 	__attribute__((unused)) bool ret = _cpuCacheBuckets[cpu].alloc(obj);
		// 	assert(ret);
		// 	assert(obj != nullptr);

		// 	return obj;
		// }

		// // Slow-path if CPU is not known
		// return allocateOne();

}

void bucket_free(cache_bucket_t *bucket, void *obj, int cpu)
{
// assert(cpu != -1);
// 		if (_cpuCacheBuckets[cpu].isInPage(obj)) {
// 			// Fast path
// 			_cpuCacheBuckets[cpu].localFree(obj);
// 		} else {
// 			// Remote free, slow-path
// 			// Get local allocator
// 			LocalAllocator *alloc = Hive::getSharedMemory().getLocalAllocator();

// 			// Get the page metadata corresponding to this allocation
// 			LocalAllocator::PageMetadata *page = alloc->getChunkMetadata(obj);

// 			// Grab bucket lock
// 			_lock.lock();

// 			if (page->inUse < objInPage()) {
// 				// Partial page, already in the partial list
// 				page->inUse--;
// 				// Set "next"
// 				*((void **)obj) = page->freelist;
// 				page->freelist = obj;

// 				if (page->inUse == 0) {
// 					// Free page now, remove from partial list? For now return
// 					// alloc->returnChunk(page, cpu);
// 				}
// 			} else {
// 				// Put in partial list
// 				// Partial page, already in the partial list
// 				page->inUse--;
// 				// Set "next"
// 				*((void **)obj) = nullptr;
// 				page->freelist = obj;

// 				// Add to partial list
// 				_partial._n++;
// 				page->next = _partial._list;
// 				_partial._list = page;
// 			}

// 			_lock.unlock();
// 		}
}

void slab_init()
{
		// SharedMemory &shmem = Hive::getSharedMemory();
		// shmem.lock();
		// buckets = (CacheBucket *)shmem.getKey("CacheBuckets");
		// if (!buckets) {
		// 	buckets = (CacheBucket *)SharedMalloc::interProcessMalloc(sizeof(CacheBucket) * numBuckets);

		// 	for (size_t i = 0; i < numBuckets; ++i) {
		// 		new (&buckets[i]) CacheBucket(i + minAllocPower);
		// 	}

		// 	shmem.setKey("CacheBuckets", buckets);
		// }
		// shmem.unlock();
}

void* salloc(size_t size, int cpu)
{
		// size_t nextPO2 = 64 - __builtin_clzll(size - 1);
		// if (nextPO2 < minAllocPower)
		// 	nextPO2 = minAllocPower;

		// if (nextPO2 > (numBuckets + minAllocPower)) {
		// 	return SharedMalloc::malloc(size);
		// }

		// assert(size <= (1ULL << nextPO2));

		// CacheBucket &bucket = buckets[nextPO2 - minAllocPower];

		// return bucket.alloc(cpu);

}

void sfree(void *ptr, size_t size, int cpu)
{
		// size_t nextPO2 = 64 - __builtin_clzll(size - 1);
		// if (nextPO2 < minAllocPower)
		// 	nextPO2 = minAllocPower;

		// if (nextPO2 > (numBuckets + minAllocPower)) {
		// 	return SharedMalloc::free(ptr, size);
		// }

		// CacheBucket &bucket = buckets[nextPO2 - minAllocPower];
		// bucket.free(ptr, cpu);

}