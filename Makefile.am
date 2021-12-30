#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)

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

libnosv_la_CPPFLAGS=$(AM_CPPFLAGS) $(ovni_CPPFLAGS) -DINSTALLED_CONFIG_DIR=\"$(datadir)\" $(asan_CPPFLAGS)
libnosv_la_CFLAGS=$(PTHREAD_CFLAGS) $(libnuma_CFLAGS) $(nosv_CFLAGS) $(asan_CFLAGS)
libnosv_la_LDFLAGS=$(PTHREAD_CFLAGS) $(PTHREAD_LIBS) $(libnuma_LIBS) $(ovni_LIBS) $(shm_LIBS) $(asan_LDFLAGS) -lm

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
    src/bootstrap.c		\
	src/config/config.c \
	src/config/toml.c \
	src/hardware/cpus.c \
	src/hardware/locality.c \
	src/hardware/pids.c \
	src/hardware/threads.c \
	src/hwcounters/cpuhwcounters.c \
	src/hwcounters/hwcounters.c \
	src/hwcounters/taskhwcounters.c \
	src/hwcounters/threadhwcounters.c \
	src/memory/backbone.c \
	src/memory/slab.c \
	src/memory/sharedmemory.c \
	src/monitoring/cpumonitor.c \
	src/monitoring/cpustatistics.c \
	src/monitoring/monitoring.c \
	src/scheduler/scheduler.c \
	src/system/tasks.c

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
	src/include/generic/bitset.h \
	src/include/generic/clock.h \
	src/include/generic/condvar.h \
	src/include/generic/heap.h \
	src/include/generic/list.h \
	src/include/generic/mutex.h \
	src/include/generic/proc.h \
	src/include/generic/ringbuffer.h \
	src/include/generic/signalmutex.h \
	src/include/generic/spinlock.h \
	src/include/hardware/cpus.h \
	src/include/hardware/eventqueue.h \
	src/include/hardware/locality.h \
	src/include/hardware/pids.h \
	src/include/hardware/threads.h \
	src/include/hwcounters/cpuhwcounters.h \
	src/include/hwcounters/hwcounters.h \
	src/include/hwcounters/supportedhwcounters.h \
	src/include/hwcounters/taskhwcounters.h \
	src/include/hwcounters/threadhwcounters.h \
	src/include/memory/asan.h \
	src/include/memory/backbone.h \
	src/include/memory/sharedmemory.h \
	src/include/memory/slab.h \
	src/include/scheduler/dtlock.h \
	src/include/scheduler/mpsc.h \
	src/include/scheduler/scheduler.h \
	src/include/scheduler/spsc.h \
	src/include/system/tasks.h \
	tests/common/tap.h \
	tests/include/test.h

if HAVE_PAPI
noinst_HEADERS += \
	src/include/hwcounters/papi/papicpuhwcounters.h \
	src/include/hwcounters/papi/papihwcounters.h \
	src/include/hwcounters/papi/papitaskhwcounters.h \
	src/include/hwcounters/papi/papithreadhwcounters.h
endif

subincludedir = $(includedir)/nosv
subinclude_HEADERS = \
	api/nosv/affinity.h

# Install config file
data_DATA = nosv.toml

EXTRA_DIST = nosv.toml
