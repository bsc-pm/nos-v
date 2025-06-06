#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)

ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = -I$(srcdir)/src/include -I$(srcdir)/api
SUBDIRS = . tests

# See info page of libtool "Updating version info"
# https://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
lib_current = 0
lib_revision = 0
lib_age = 0

# Expected shared object numbers
lib_major = $(shell expr $(lib_current) - $(lib_age))
lib_suffix = $(lib_major).$(lib_age).$(lib_revision)

libnosv_la_CPPFLAGS=$(AM_CPPFLAGS) $(nosv_CPPFLAGS) $(libnuma_CPPFLAGS) $(pthreadnp_CPPFLAGS)  $(ovni_CPPFLAGS) -DINSTALLED_CONFIG_DIR=\"$(datadir)\" $(asan_CPPFLAGS) $(CODE_COVERAGE_CPPFLAGS)
libnosv_la_CFLAGS=$(PTHREAD_CFLAGS) $(nosv_CFLAGS) $(asan_CFLAGS) $(CODE_COVERAGE_CFLAGS)
libnosv_la_LDFLAGS=$(PTHREAD_CFLAGS) $(PTHREAD_LIBS) $(libnuma_LIBS) $(ovni_LIBS) $(shm_LIBS) $(asan_LDFLAGS) $(CODE_COVERAGE_LIBS) -lm -ldl

include $(top_srcdir)/aminclude_static.am

# TOML and INSTR macros do not play well with lcov
CODE_COVERAGE_LCOV_OPTIONS = --exclude */config.c --exclude */instr.h --exclude */xmmintrin.h
CODE_COVERAGE_DIRECTORY = $(abs_top_builddir)
CODE_COVERAGE_OUTPUT_DIRECTORY = coverage-report

clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean

# Hardware counters
if HAVE_PAPI
libnosv_la_CFLAGS += $(papi_CFLAGS) -DHAVE_PAPI
libnosv_la_LDFLAGS += $(papi_LIBS)
endif

lib_LTLIBRARIES = libnosv.la

# Install the pkg-config file; the directory is set using
# PKG_INSTALLDIR in configure.ac
pkgconfig_DATA = nos-v.pc

libnosv_la_SOURCES = \
	src/alpi.c \
	src/bootstrap.c \
	src/error.c \
	src/config/config.c \
	src/config/toml.c \
	src/hardware/topology.c \
	src/hardware/pids.c \
	src/hardware/threads.c \
	src/hwcounters/cpuhwcounters.c \
	src/hwcounters/hwcounters.c \
	src/hwcounters/taskhwcounters.c \
	src/hwcounters/threadhwcounters.c \
	src/memory/backbone.c \
	src/memory/sharedmemory.c \
	src/monitoring/cpumonitor.c \
	src/monitoring/cpustats.c \
	src/monitoring/monitoring.c \
	src/scheduler/governor.c \
	src/scheduler/scheduler.c \
	src/support/affinity.c \
	src/support/barrier.c \
	src/support/mutex.c \
	src/support/cond.c \
	src/system/tasks.c

if USE_ASAN
libnosv_la_SOURCES += \
	src/memory/slab-asan.c
else
libnosv_la_SOURCES += \
	src/memory/slab.c
endif

if HAVE_OVNI
libnosv_la_SOURCES += \
	src/instr.c
endif

if HAVE_PAPI
libnosv_la_SOURCES += \
	src/hwcounters/papi/papicpuhwcounters.c \
	src/hwcounters/papi/papihwcounters.c \
	src/hwcounters/papi/papitaskhwcounters.c \
	src/hwcounters/papi/papithreadhwcounters.c
endif

include_HEADERS = \
	api/nosv.h

noinst_HEADERS = \
	src/include/defaults.h \
	src/include/common.h \
	src/include/compat.h \
	src/include/compiler.h \
	src/include/instr.h \
	src/include/nosv-internal.h \
	src/include/config/config.h \
	src/include/config/configspec.h \
	src/include/config/toml.h \
	src/include/generic/arch.h \
	src/include/generic/arch/arm64.h \
	src/include/generic/arch/power.h \
	src/include/generic/arch/x86.h \
	src/include/generic/accumulator.h \
	src/include/generic/bitset.h \
	src/include/generic/chrono.h \
	src/include/generic/clock.h \
	src/include/generic/cpuset.h \
	src/include/generic/condvar.h \
	src/include/generic/futex.h \
	src/include/generic/heap.h \
	src/include/generic/hashfunction.h \
	src/include/generic/hashtable.h \
	src/include/generic/list.h \
	src/include/generic/mutex.h \
	src/include/generic/proc.h \
	src/include/generic/ringbuffer.h \
	src/include/generic/signalmutex.h \
	src/include/generic/spinlock.h \
	src/include/generic/tree.h \
	src/include/generic/_null.h \
	src/include/hardware/eventqueue.h \
	src/include/hardware/pids.h \
	src/include/hardware/threads.h \
	src/include/hardware/topology.h \
	src/include/hwcounters/cpuhwcounters.h \
	src/include/hwcounters/hwcounters.h \
	src/include/hwcounters/supportedhwcounters.h \
	src/include/hwcounters/taskhwcounters.h \
	src/include/hwcounters/threadhwcounters.h \
	src/include/memory/backbone.h \
	src/include/memory/sharedmemory.h \
	src/include/memory/slab.h \
	src/include/monitoring/cpumonitor.h \
	src/include/monitoring/cpustats.h \
	src/include/monitoring/monitoring.h \
	src/include/monitoring/monitoringsupport.h \
	src/include/monitoring/taskmonitor.h \
	src/include/monitoring/taskstats.h \
	src/include/monitoring/tasktypestats.h \
	src/include/scheduler/cpubitset.h \
	src/include/scheduler/dtlock.h \
	src/include/scheduler/governor.h \
	src/include/scheduler/mpsc.h \
	src/include/scheduler/scheduler.h \
	src/include/scheduler/spsc.h \
	src/include/support/affinity.h \
	src/include/system/taskgroup.h \
	src/include/system/tasks.h \
	tests/common/tap.h \
	tests/common/utils.h \
	tests/include/test.h \
	tests/include/unit.h

if HAVE_PAPI
noinst_HEADERS += \
	src/include/hwcounters/papi/papicpuhwcounters.h \
	src/include/hwcounters/papi/papihwcounters.h \
	src/include/hwcounters/papi/papitaskhwcounters.h \
	src/include/hwcounters/papi/papithreadhwcounters.h
endif

subincludedir = $(includedir)/nosv
subinclude_HEADERS = \
	api/nosv/affinity.h \
	api/nosv/alpi.h \
	api/nosv/alpi-defs.h \
	api/nosv/error.h \
	api/nosv/hwinfo.h \
	api/nosv/memory.h

# Install config file
data_DATA = nosv.toml

EXTRA_DIST = nosv.toml
