/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <stddef.h>

// Marks memory region [addr, addr+size) as unaddressable.
// This memory must be previously allocated by the user program. Accessing
// addresses in this region from instrumented code is forbidden until
// this region is unpoisoned. This function is not guaranteed to poison
// the whole region - it may poison only subregion of [addr, addr+size) due
// to ASan alignment restrictions.
// Method is NOT thread-safe in the sense that no two threads can
// (un)poison memory in the same memory region simultaneously.
void __asan_poison_memory_region(void const volatile *addr, size_t size);
// Marks memory region [addr, addr+size) as addressable.
// This memory must be previously allocated by the user program. Accessing
// addresses in this region is allowed until this region is poisoned again.
// This function may unpoison a superregion of [addr, addr+size) due to
// ASan alignment restrictions.
// Method is NOT thread-safe in the sense that no two threads can
// (un)poison memory in the same memory region simultaneously.
void __asan_unpoison_memory_region(void const volatile *addr, size_t size);

// User code should use macros instead of functions.
#if defined(__SANITIZE_ADDRESS__)
static inline void asan_poison(void const volatile *addr, size_t size)
{
	__asan_poison_memory_region(addr, size);
}
static inline void asan_unpoison(void const volatile *addr, size_t size)
{
	__asan_unpoison_memory_region(addr, size);
}
#else
static inline void asan_poison(void const volatile *addr, size_t size) {}
static inline void asan_unpoison(void const volatile *addr, size_t size) {}
#endif

