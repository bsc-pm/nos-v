/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/utils.h"

typedef struct test {
	int ntests;
	int expected;
	pthread_spinlock_t lock;
} test_t;

static inline void test_init(test_t *test, int ntests)
{
	printf("pl%d\n", ntests);
	test->ntests = 0;
	test->expected = ntests;
	pthread_spin_init(&test->lock, 0);
}

#define TEST_OPTION_PARALLEL 0

static inline void test_option(test_t *test, int option, int value)
{
	printf("op%d %d\n", option, value);
}

static inline void test_ok(test_t *test, const char *fmt, ...)
{
	pthread_spin_lock(&test->lock);
	va_list args;
	va_start(args, fmt);
	test->ntests++;

	printf("pa");
	vprintf(fmt, args);
	printf("\n");
	pthread_spin_unlock(&test->lock);
}

static inline void test_fail(test_t *test, const char *fmt, ...)
{
	pthread_spin_lock(&test->lock);
	va_list args;
	va_start(args, fmt);
	test->ntests++;

	printf("fa");
	vprintf(fmt, args);
	printf("\n");
	pthread_spin_unlock(&test->lock);
}

static inline void test_xfail(test_t *test, const char *reason, const char *fmt, ...)
{
	pthread_spin_lock(&test->lock);
	va_list args;
	va_start(args, fmt);
	test->ntests++;

	printf("xf");
	vprintf(fmt, args);
	printf("####%s\n", reason);
	pthread_spin_unlock(&test->lock);
}

static inline void test_skip(test_t *test, const char *reason, const char *fmt, ...)
{
	pthread_spin_lock(&test->lock);
	va_list args;
	va_start(args, fmt);
	test->ntests++;

	printf("sk");
	vprintf(fmt, args);
	printf("####%s\n", reason);
	pthread_spin_unlock(&test->lock);
}

static inline void test_error(test_t *test, const char *fmt, ...)
{
	pthread_spin_lock(&test->lock);
	va_list args;
	va_start(args, fmt);
	test->ntests++;

	printf("bo");
	vprintf(fmt, args);
	printf("\n");
	pthread_spin_unlock(&test->lock);
}

static inline void test_check(test_t *test, int check, const char *fmt, ...)
{
	pthread_spin_lock(&test->lock);
	va_list args;
	va_start(args, fmt);
	test->ntests++;

	if (check) {
		// Pass
		printf("pa");
	} else {
		printf("fa");
	}

	vprintf(fmt, args);
	printf("\n");
	pthread_spin_unlock(&test->lock);
}

// Check with a timeout in ms
// Note that condition _will_ be evaluated multiple times
// There are two variants of this timeout check, depending on the specific situation:
// - test_check_timeout uses "usleep" to wait, which will block the current task/cpu and thus
//   it is probably a bad idea to use from inside a nOS-V thread
// - test_check_waitfor uses "nosv_waitfor" to wait, which will be usable from a nOS-V thread,
//   but should be avoided when there is a risk of nOS-V hanging completely, since then the
//   waitfor will not return
// - test_check_waitfor_api is the same as the previous test_check_waitfor, but it allows
//   to specify the waitfor API function name. The waitfor function must have the same
//   function prototype as the nosv_waitfor

#define test_check_timeout(test, condition, timeout, fmt, ...) __extension__({ \
	int _local_timeout = ((int) timeout);                                      \
	int _increment = 1;                                                        \
	assert(_local_timeout > 0);                                                \
	int _evaluated_condition = (condition);                                    \
	while (_local_timeout > 0 && !_evaluated_condition) {                      \
		usleep(_increment * 1000);                                             \
		_local_timeout -= _increment;                                          \
		_increment *= 2;                                                       \
		if (_increment > _local_timeout)                                       \
			_increment = _local_timeout;                                       \
		_evaluated_condition = (condition);                                    \
	}                                                                          \
	test_check((test), _evaluated_condition, (fmt), ##__VA_ARGS__);            \
})

#define test_check_waitfor_api(test, condition, timeout, api, fmt, ...) __extension__({ \
	int64_t _local_timeout = ((int64_t) timeout);                                       \
	int64_t _increment = 1;                                                             \
	assert(_local_timeout > 0);                                                         \
	int _evaluated_condition = (condition);                                             \
	while (_local_timeout > 0 && !_evaluated_condition) {                               \
		CHECK(api(_increment * 1000LL * 1000LL, NULL));                                 \
		_local_timeout -= _increment;                                                   \
		_increment *= 2;                                                                \
		if (_increment > _local_timeout)                                                \
			_increment = _local_timeout;                                                \
		_evaluated_condition = (condition);                                             \
	}                                                                                   \
	test_check((test), _evaluated_condition, (fmt), ##__VA_ARGS__);                     \
})

#define test_check_waitfor(test, condition, timeout, fmt, ...) \
	test_check_waitfor_api(test, condition, timeout, nosv_waitfor, fmt, ##__VA_ARGS__)

static inline void test_end(test_t *test)
{
	assert(test->ntests == test->expected);
	pthread_spin_destroy(&test->lock);
}
