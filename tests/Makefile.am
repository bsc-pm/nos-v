#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)

.NOTPARALLEL:

AM_CFLAGS = -I$(top_srcdir)/tests/include $(PTHREAD_CFLAGS) -I$(top_srcdir)/api $(ovni_CPPFLAGS)
AM_LDFLAGS = -L$(top_builddir)/.libs -rpath $(abs_top_builddir)/.libs $(top_builddir)/libnosv.la $(ovni_LIBS)

a_tests = basic-init.test \
	basic-init-procs.test \
	basic-mix.test \
	basic-crash.test \
	basic-inline.test \
	basic-attach.test \
	basic-attach-external.test \
	basic-schedpoint.test \
	basic-yield.test

check_PROGRAMS = driver $(a_tests)
TESTS = $(a_tests)

basic_init_test_SOURCES = basic/init.c
basic_init_test_CFLAGS = $(AM_CFLAGS)
basic_init_test_LDFLAGS = $(AM_LDFLAGS)

basic_init_procs_test_SOURCES = basic/init-procs.c
basic_init_procs_test_CFLAGS = $(AM_CFLAGS) -I$(top_srcdir)/src/include
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
basic_attach_external_test_CFLAGS = $(AM_CFLAGS) -I$(top_srcdir)/src/include
basic_attach_external_test_LDFLAGS = $(AM_LDFLAGS)

basic_schedpoint_test_SOURCES = basic/schedpoint.c
basic_schedpoint_test_CFLAGS = $(AM_CFLAGS)
basic_schedpoint_test_LDFLAGS = $(AM_LDFLAGS)

basic_yield_test_SOURCES = basic/yield.c
basic_yield_test_CFLAGS = $(AM_CFLAGS)
basic_yield_test_LDFLAGS = $(AM_LDFLAGS)

driver_SOURCES = common/driver.c
driver_CFLAGS = -O3 -g -Wall -Wpedantic $(PTHREAD_CFLAGS)
driver_LDFLAGS = -lrt $(PTHREAD_LIBS)

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/tests/wrapper.sh $(top_builddir)/tests/driver $(top_srcdir)/tests/tap-driver.sh
EXTRA_DIST = tap-driver.sh driver wrapper.sh $(TESTS)
