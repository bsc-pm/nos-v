#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)

.NOTPARALLEL:

AM_CFLAGS = $(asan_CFLAGS) -D_GNU_SOURCE -I$(top_srcdir)/tests/include $(PTHREAD_CFLAGS) -I$(top_srcdir)/src/include -I$(top_srcdir)/api $(ovni_CPPFLAGS) -g3 -O2
AM_LDFLAGS = $(asan_LDFLAGS) -L$(top_builddir)/.libs -rpath $(abs_top_builddir)/.libs $(top_builddir)/libnosv.la $(ovni_LIBS)

unit_CFLAGS = -D_GNU_SOURCE -I$(top_srcdir)/tests/include $(PTHREAD_CFLAGS) -I$(top_srcdir)/src/include -I$(top_srcdir)/api -g3 -O2
unit_LIBS = -lrt $(PTHREAD_LIBS)
unit_SOURCES = unit/main.c

basic_tests = basic-error-code.test \
	basic-init.test \
	basic-reinit.test \
	basic-init-multicall.test \
	basic-mix.test \
	basic-crash.test \
	basic-turbo.test \
	basic-inline.test \
	basic-affinity-support.test \
	basic-attach.test \
	basic-attach-external.test \
	basic-barrier.test \
	basic-mutex.test \
	basic-schedpoint.test \
	basic-yield.test \
	basic-priority.test \
	basic-deadline.test \
	basic-deadline-wake.test \
	basic-parallel.test \
	basic-parallel-cancel.test \
	basic-parallel-init.test \
	basic-alpi.test \
	basic-submit-window.test \
	basic-multi_pause.test

# Tests that won't work with AddressSanitizer
if !USE_ASAN
basic_tests += \
	basic-init-procs.test
endif

regression_tests = regression-attach-is.test \
	regression-parallel-tasks-cross-process.test \
	regression-parallel-tasks-yield.test \
	regression-parallel-tasks-submission.test

unit_tests = unit-backbone-pressure.test \
	unit-dtlock.test \
	unit-ring-buffer.test \
	unit-heap.test

check_PROGRAMS = driver $(basic_tests) $(regression_tests) $(unit_tests)
TESTS = $(basic_tests) $(regression_tests) $(unit_tests)

basic_error_code_test_SOURCES = basic/error-code.c
basic_error_code_test_CFLAGS = $(AM_CFLAGS)
basic_error_code_test_LDFLAGS = $(AM_LDFLAGS)

basic_init_test_SOURCES = basic/init.c
basic_init_test_CFLAGS = $(AM_CFLAGS)
basic_init_test_LDFLAGS = $(AM_LDFLAGS)

basic_reinit_test_SOURCES = basic/reinit.c
basic_reinit_test_CFLAGS = $(AM_CFLAGS)
basic_reinit_test_LDFLAGS = $(AM_LDFLAGS)

basic_init_multicall_test_SOURCES = basic/init-multicall.c
basic_init_multicall_test_CFLAGS = $(AM_CFLAGS)
basic_init_multicall_test_LDFLAGS = $(AM_LDFLAGS)

basic_init_procs_test_SOURCES = basic/init-procs.c
basic_init_procs_test_CFLAGS = $(AM_CFLAGS)
basic_init_procs_test_LDFLAGS = $(AM_LDFLAGS)

basic_mix_test_SOURCES = basic/mix.c
basic_mix_test_CFLAGS = $(AM_CFLAGS)
basic_mix_test_LDFLAGS = $(AM_LDFLAGS)

basic_crash_test_SOURCES = basic/crash.c
basic_crash_test_CFLAGS = $(AM_CFLAGS)
basic_crash_test_LDFLAGS = $(AM_LDFLAGS)

basic_turbo_test_SOURCES = basic/turbo.c
basic_turbo_test_CFLAGS = $(AM_CFLAGS)
basic_turbo_test_LDFLAGS = $(AM_LDFLAGS)

basic_inline_test_SOURCES = basic/inline.c
basic_inline_test_CFLAGS = $(AM_CFLAGS)
basic_inline_test_LDFLAGS = $(AM_LDFLAGS)

basic_affinity_support_test_SOURCES = basic/affinity-support.c
basic_affinity_support_test_CFLAGS = $(AM_CFLAGS) $(pthreadnp_CPPFLAGS) -std=c11
basic_affinity_support_test_LDFLAGS = $(AM_LDFLAGS)

basic_attach_test_SOURCES = basic/attach.c
basic_attach_test_CFLAGS = $(AM_CFLAGS)
basic_attach_test_LDFLAGS = $(AM_LDFLAGS)

basic_attach_external_test_SOURCES = basic/attach-external.c
basic_attach_external_test_CFLAGS = $(AM_CFLAGS)
basic_attach_external_test_LDFLAGS = $(AM_LDFLAGS)

basic_barrier_test_SOURCES = basic/barrier.c
basic_barrier_test_CFLAGS = $(AM_CFLAGS)
basic_barrier_test_LDFLAGS = $(AM_LDFLAGS)

basic_mutex_test_SOURCES = basic/mutex.c
basic_mutex_test_CFLAGS = $(AM_CFLAGS) -std=c11
basic_mutex_test_LDFLAGS = $(AM_LDFLAGS)

basic_schedpoint_test_SOURCES = basic/schedpoint.c
basic_schedpoint_test_CFLAGS = $(AM_CFLAGS)
basic_schedpoint_test_LDFLAGS = $(AM_LDFLAGS)

basic_yield_test_SOURCES = basic/yield.c
basic_yield_test_CFLAGS = $(AM_CFLAGS)
basic_yield_test_LDFLAGS = $(AM_LDFLAGS)

basic_priority_test_SOURCES = basic/priority.c
basic_priority_test_CFLAGS = $(AM_CFLAGS)
basic_priority_test_LDFLAGS = $(AM_LDFLAGS)

basic_deadline_test_SOURCES = basic/deadline.c
basic_deadline_test_CFLAGS = $(AM_CFLAGS)
basic_deadline_test_LDFLAGS = $(AM_LDFLAGS)

basic_deadline_wake_test_SOURCES = basic/deadline-wake.c
basic_deadline_wake_test_CFLAGS = $(AM_CFLAGS)
basic_deadline_wake_test_LDFLAGS = $(AM_LDFLAGS) -lrt

basic_parallel_test_SOURCES = basic/parallel.c
basic_parallel_test_CFLAGS = $(AM_CFLAGS)
basic_parallel_test_LDFLAGS = $(AM_LDFLAGS)

basic_parallel_cancel_test_SOURCES = basic/parallel-cancel.c
basic_parallel_cancel_test_CFLAGS = $(AM_CFLAGS)
basic_parallel_cancel_test_LDFLAGS = $(AM_LDFLAGS)

basic_parallel_init_test_SOURCES = basic/parallel-init.c
basic_parallel_init_test_CFLAGS = $(AM_CFLAGS)
basic_parallel_init_test_LDFLAGS = $(AM_LDFLAGS)

basic_alpi_test_SOURCES = basic/alpi.c
basic_alpi_test_CFLAGS = $(AM_CFLAGS)
basic_alpi_test_LDFLAGS = $(AM_LDFLAGS)

basic_submit_window_test_SOURCES = basic/submit-window.c
basic_submit_window_test_CFLAGS = $(AM_CFLAGS)
basic_submit_window_test_LDFLAGS = $(AM_LDFLAGS)

basic_multi_pause_test_SOURCES = basic/multi_pause.c
basic_multi_pause_test_CFLAGS = $(AM_CFLAGS)
basic_multi_pause_test_LDFLAGS = $(AM_LDFLAGS)

regression_attach_is_test_SOURCES = regression/attach-is.c
regression_attach_is_test_CFLAGS = $(AM_CFLAGS)
regression_attach_is_test_LDFLAGS = $(AM_LDFLAGS)

regression_parallel_tasks_submission_test_SOURCES = regression/parallel-tasks-submission.c
regression_parallel_tasks_submission_test_CFLAGS = $(AM_CFLAGS)
regression_parallel_tasks_submission_test_LDFLAGS = $(AM_LDFLAGS)

regression_parallel_tasks_cross_process_test_SOURCES = regression/parallel-tasks-cross-process.c
regression_parallel_tasks_cross_process_test_CFLAGS = $(AM_CFLAGS)
regression_parallel_tasks_cross_process_test_LDFLAGS = $(AM_LDFLAGS)

regression_parallel_tasks_yield_test_SOURCES = regression/parallel-tasks-yield.c
regression_parallel_tasks_yield_test_CFLAGS = $(AM_CFLAGS)
regression_parallel_tasks_yield_test_LDFLAGS = $(AM_LDFLAGS)

unit_backbone_pressure_test_SOURCES = unit/test-backbone-pressure.c $(top_srcdir)/src/memory/backbone.c $(unit_SOURCES)
unit_backbone_pressure_test_CFLAGS = $(unit_CFLAGS)
unit_backbone_pressure_test_LDFLAGS = $(unit_LIBS)

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

if USE_ASAN
driver_CFLAGS += -DNO_PARALLEL_TEST
endif

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/tests/wrapper.sh $(top_srcdir) $(top_builddir)/tests/driver $(top_srcdir)/tests/tap-driver.sh
EXTRA_DIST = tap-driver.sh wrapper.sh
