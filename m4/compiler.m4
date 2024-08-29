#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)

AC_DEFUN([AX_CHECK_C11], [
	AX_CHECK_COMPILE_FLAG([-std=c11], [
		CFLAGS="-std=c11 $CFLAGS"
	], [
		AC_MSG_ERROR([Compiler not compatible with C11])
	])
])

AC_DEFUN([AX_CHECK_CC_VERSION], [
	AC_MSG_CHECKING([the ${CC} version])
	if test x"$CC" != x"" ; then
		CC_VERSION=$(${CC} --version | head -1)
	fi
	AC_MSG_RESULT([$CC_VERSION])
	AC_SUBST([CC_VERSION])
])

AC_DEFUN([AX_PREPARE_SANITIZER_FLAGS], [
	AC_MSG_CHECKING([if we should enable sanitizers])

	AC_ARG_ENABLE([asan], [AS_HELP_STRING([--enable-asan],
		[Enables address sanitizing @<:@default=disabled@:>@])])

	AC_ARG_ENABLE([ubsan], [AS_HELP_STRING([--enable-ubsan],
		[Enables undefined behaviour sanitizing @<:@default=disabled@:>@])])

	AM_CONDITIONAL(USE_ASAN, test x"${enable_asan}" = x"yes")

	AS_IF([test "$enable_asan" = yes],[
		# ASAN enabled
		asan_CPPFLAGS="-fsanitize=address"
		asan_LDFLAGS="-fsanitize=address"
		asan_CFLAGS="-fsanitize=address"
		AC_MSG_RESULT([yes])
	],[
		AS_IF([test "$enable_ubsan" = yes],[
			# UBSan enabled
			asan_CPPFLAGS="-fsanitize=undefined -fno-sanitize-recover=all"
			asan_LDFLAGS="-fsanitize=undefined -fno-sanitize-recover=all"
			asan_CFLAGS="-fsanitize=undefined -fno-sanitize-recover=all"
			AC_MSG_RESULT([yes])
		], [
			# ASAN and UBSan disabled
			asan_CPPFLAGS=""
			asan_LDFLAGS=""
			asan_CFLAGS=""
			AC_MSG_RESULT([no])
		])
	])

	AC_SUBST(asan_LDFLAGS)
	AC_SUBST(asan_CPPFLAGS)
	AC_SUBST(asan_CFLAGS)
])

AC_DEFUN([AX_FIXUP_CC_FLAGS], [
	AX_COMPILER_VENDOR

	if test x"$ax_cv_c_compiler_vendor" == x"clang" ; then
		nosv_CFLAGS="${nosv_CFLAGS} -Wno-gnu-zero-variadic-macro-arguments"
	fi

	AC_SUBST([nosv_CFLAGS])
])

AC_DEFUN([AX_PREPARE_CC_FLAGS], [
	AC_ARG_ENABLE([debug], [AS_HELP_STRING([--enable-debug],
		[Adds compiler debug flags and enables additional internal debugging mechanisms @<:@default=disabled@:>@])])

	nosv_COMMON_CFLAGS="-D_GNU_SOURCE -Wall -Wpedantic -Wimplicit-fallthrough -Werror-implicit-function-declaration -Wstrict-prototypes"

	AS_IF([test "$enable_debug" = yes],[
		# Debug is enabled
		nosv_CPPFLAGS=""
		nosv_CFLAGS="${nosv_COMMON_CFLAGS} -O0 -g3"
	],[
		# Debug is disabled
		nosv_CPPFLAGS=""
		nosv_CFLAGS="${nosv_COMMON_CFLAGS} -O3 -g -DNDEBUG"
	])

	AC_SUBST(nosv_CPPFLAGS)
	AC_SUBST(nosv_CFLAGS)

	# Disable autoconf default compilation flags
	: ${CPPFLAGS=""}
	: ${CFLAGS=""}
])
