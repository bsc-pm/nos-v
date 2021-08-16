#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)

.NOTPARALLEL:

AM_CFLAGS = -I$(top_srcdir)/tests/include $(PTHREAD_CFLAGS) -I$(top_builddir)/api
AM_LDFLAGS = -L$(top_builddir)/.libs -rpath $(abs_top_builddir)/.libs $(top_builddir)/libnosv.la

a_tests = basic-init.test \
	basic-mix.test

check_PROGRAMS = driver $(a_tests)
TESTS = $(a_tests)

basic_init_test_SOURCES = basic/init.c
basic_init_test_CFLAGS = $(AM_CFLAGS)
basic_init_test_LDFLAGS = $(AM_LDFLAGS)

basic_mix_test_SOURCES = basic/mix.c
basic_mix_test_CFLAGS = $(AM_CFLAGS)
basic_mix_test_LDFLAGS = $(AM_LDFLAGS)

driver_SOURCES = common/driver.c
driver_CFLAGS = -O3 -g -Wall -Wpedantic $(PTHREAD_CFLAGS)
driver_LDFLAGS = -lrt $(PTHREAD_LIBS)

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/tests/wrapper.sh $(top_srcdir)/build-aux/tap-driver.sh
EXTRA_DIST = build-aux/tap-driver.sh $(TESTS)
