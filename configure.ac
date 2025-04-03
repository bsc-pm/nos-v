#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021-2024 Barcelona Supercomputing Center (BSC)

# nOS-V Version and Copyright

m4_define([nosv_version], [3.1.0])
m4_define([nosv_license], ["GPL3"])
m4_define([nosv_copyright], ["2021-2024 Barcelona Supercomputing Center (BSC)"])

AC_PREREQ([2.69])
AC_INIT([nOS-V], [nosv_version], [david.alvarez@bsc.es])
AC_CONFIG_SRCDIR([src/bootstrap.c])
AC_CONFIG_AUX_DIR([config-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

# Prepare compilation flags before AC_PROG_CC
AX_PREPARE_CC_FLAGS

# Parse ASAN options
AX_PREPARE_SANITIZER_FLAGS

AX_CODE_COVERAGE

# Look for a valid C compiler
AC_PROG_CC

# Retrieve compiler version
AX_CHECK_CC_VERSION

# Set C11 standard
AX_CHECK_C11

# Fixup CC Flags
AX_FIXUP_CC_FLAGS

AM_PROG_AR

AM_INIT_AUTOMAKE([foreign dist-bzip2 no-dist-gzip subdir-objects -Wall tar-pax -Wno-portability])
AM_SILENT_RULES([yes])

AC_CHECK_CACHE

LT_INIT([shared disable-static pic-only])
AC_SUBST([LIBTOOL_DEPS])

# Dependencies
PKG_PROG_PKG_CONFIG
PKG_INSTALLDIR

# Checks for libraries.
AX_PTHREAD
AC_CHECK_LIBNUMA
AC_CHECK_OVNI
AC_CHECK_PAPI

AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

full_top_srcdir=$(readlink -f ${srcdir})
AC_SUBST([full_top_srcdir])

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h unistd.h])

# Checks for shared memory functions in librt
AC_CHECK_LIB([rt], [shm_open], [shm_LIBS="${shm_LIBS} -lrt"], [AC_MSG_ERROR([shm functions not found])])
AC_SUBST(shm_LIBS)

# Check for non-portable pthread functions
AC_CHECK_FUNCS([pthread_getattr_default_np],[pthreadnp_CPPFLAGS="${pthreadnp_CPPFLAGS} -DHAVE_pthread_getattr_default_np"],[])
AC_SUBST(pthreadnp_CPPFLAGS)

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_SIZE_T
AC_C_RESTRICT

AC_CONFIG_FILES([Makefile nos-v.pc tests/Makefile])
AC_OUTPUT

_AS_ECHO([])
_AS_ECHO([])
_AS_ECHO([Configuration summary:])
_AS_ECHO([   Compiler version... ${CC_VERSION}])
_AS_ECHO([   Compiler CPPFLAGS... ${nosv_CPPFLAGS} ${asan_CPPFLAGS} ${pthreadnp_CPPFLAGS} ${CODE_COVERAGE_CPPFLAGS} ${CPPFLAGS}])
_AS_ECHO([   Compiler CFLAGS... ${nosv_CFLAGS} ${asan_CFLAGS} ${CODE_COVERAGE_CFLAGS} ${CFLAGS}])

_AS_ECHO([])
_AS_ECHO_N([   Ovni is enabled... ])
if test x"${ac_use_ovni}" = x"yes" ; then
	AC_MSG_RESULT([yes])
	_AS_ECHO([   Ovni CPPFLAGS... ${ovni_CPPFLAGS}])
	_AS_ECHO([   Ovni LIBS... ${ovni_LIBS}])
else
	AC_MSG_RESULT([no])
fi

_AS_ECHO([])
_AS_ECHO_N([   PAPI is enabled... ])
if test x"${ac_use_papi}" = x"yes" ; then
	AC_MSG_RESULT([yes])
	_AS_ECHO([   PAPI CPPFLAGS... ${papi_CPPFLAGS}])
	_AS_ECHO([   PAPI LIBS... ${papi_LIBS}])
else
	AC_MSG_RESULT([no])
fi
_AS_ECHO([])
