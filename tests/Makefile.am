#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)

.NOTPARALLEL:

AM_CFLAGS = $(asan_CFLAGS) -D_GNU_SOURCE -I$(top_srcdir)/tests/include $(PTHREAD_CFLAGS) -I$(top_srcdir)/src/include -I$(top_srcdir)/api $(ovni_CPPFLAGS)
AM_LDFLAGS = $(asan_LDFLAGS) -L$(top_builddir)/.libs -rpath $(abs_top_builddir)/.libs $(top_builddir)/libnosv.la $(ovni_LIBS)

unit_CFLAGS = -D_GNU_SOURCE -I$(top_srcdir)/tests/include $(PTHREAD_CFLAGS) -I$(top_srcdir)/src/include -I$(top_srcdir)/api -g3 -O2
unit_LIBS = -lrt $(PTHREAD_LIBS)
unit_SOURCES = unit/main.c

a_tests = basic-init.test \
	basic-init-procs.test \
	basic-mix.test \
	basic-crash.test \
	basic-inline.test \
	basic-attach.test \
	basic-attach-external.test \
	basic-schedpoint.test \
	basic-yield.test \
	basic-deadline.test

unit_tests = unit-dtlock.test \
	unit-ring-buffer.test \
	unit-heap.test

check_PROGRAMS = driver $(a_tests) $(unit_tests)
TESTS = $(a_tests) $(unit_tests)

basic_init_test_SOURCES = basic/init.c
basic_init_test_CFLAGS = $(AM_CFLAGS)
basic_init_test_LDFLAGS = $(AM_LDFLAGS)

basic_init_procs_test_SOURCES = basic/init-procs.c
basic_init_procs_test_CFLAGS = $(AM_CFLAGS)
basic_init_procs_test_LDFLAGS = $(AM_LDFLAGS)

basic_mix_test_SOURCES = basic/mix.c
basic_mix_test_CFLAGS = $(AM_CFLAGS)
basic_mix_test_LDFLAGS = $(AM_LDFLAGS)

basic_crash_test_SOURCES = basic/crash.c
basic_crash_test_CFLAGS = $(AM_CFLAGS)
basic_crash_test_LDFLAGS = $(AM_LDFLAGS)

basic_inline_test_SOURCES = basic/inline.c
basic_inline_test_CFLAGS = $(AM_CFLAGS)
basic_inline_test_LDFLAGS = $(AM_LDFLAGS)

basic_attach_test_SOURCES = basic/attach.c
basic_attach_test_CFLAGS = $(AM_CFLAGS)
basic_attach_test_LDFLAGS = $(AM_LDFLAGS)

basic_attach_external_test_SOURCES = basic/attach-external.c
basic_attach_external_test_CFLAGS = $(AM_CFLAGS)
basic_attach_external_test_LDFLAGS = $(AM_LDFLAGS)

basic_schedpoint_test_SOURCES = basic/schedpoint.c
basic_schedpoint_test_CFLAGS = $(AM_CFLAGS)
basic_schedpoint_test_LDFLAGS = $(AM_LDFLAGS)

basic_yield_test_SOURCES = basic/yield.c
basic_yield_test_CFLAGS = $(AM_CFLAGS)
basic_yield_test_LDFLAGS = $(AM_LDFLAGS)

basic_deadline_test_SOURCES = basic/deadline.c
basic_deadline_test_CFLAGS = $(AM_CFLAGS)
basic_deadline_test_LDFLAGS = $(AM_LDFLAGS)

unit_dtlock_test_SOURCES = unit/test-dtlock.c $(unit_SOURCES)
unit_dtlock_test_CFLAGS = $(unit_CFLAGS)
unit_dtlock_test_LDFLAGS = $(unit_LIBS)

unit_heap_test_SOURCES = unit/test-heap.c $(unit_SOURCES)
unit_heap_test_CFLAGS = $(unit_CFLAGS)
unit_heap_test_LDFLAGS = $(unit_LIBS)

unit_ring_buffer_test_SOURCES = unit/test-ring-buffer.c $(unit_SOURCES)
unit_ring_buffer_test_CFLAGS = $(unit_CFLAGS)
unit_ring_buffer_test_LDFLAGS = $(unit_LIBS)

driver_SOURCES = common/driver.c
driver_CFLAGS = -O3 -g -Wall -Wpedantic $(PTHREAD_CFLAGS)
driver_LDFLAGS = -lrt $(PTHREAD_LIBS)

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/tests/wrapper.sh $(top_srcdir) $(top_builddir)/tests/driver $(top_srcdir)/tests/tap-driver.sh
EXTRA_DIST = tap-driver.sh wrapper.sh
