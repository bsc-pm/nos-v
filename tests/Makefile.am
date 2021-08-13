#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)

AM_CFLAGS = -I$(top_srcdir)/tests/common $(PTHREAD_CFLAGS) -I$(top_builddir)/include
AM_LDFLAGS = -L$(top_builddir)/.libs -rpath $(abs_top_builddir)/.libs $(top_builddir)/libnosv.la

a_tests = basic.test

check_PROGRAMS = $(a_tests)
TESTS = $(a_tests)

basic_test_SOURCES = basic/basic.c
basic_test_CFLAGS = $(AM_CFLAGS)
basic_test_LDFLAGS = $(AM_LDFLAGS)

# TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/build-aux/tap-driver.sh
EXTRA_DIST = build-aux/tap-driver.sh $(TESTS)
