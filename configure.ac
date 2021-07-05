#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)

# nOS-V Version and Copyright

m4_define([nosv_version], [1.0.0])
m4_define([nosv_license], ["GPL3"])
m4_define([nosv_copyright], ["2021 Barcelona Supercomputing Center (BSC)"])

AC_PREREQ([2.69])
AC_INIT([nOS-V], [nosv_version], [david.alvarez@bsc.es])
AC_CONFIG_SRCDIR([src/bootstrap.c])
AC_CONFIG_AUX_DIR([config-aux])
AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

AM_INIT_AUTOMAKE([foreign dist-bzip2 no-dist-gzip subdir-objects -Wall tar-pax -Wno-portability])
AM_SILENT_RULES([yes])

AC_CHECK_CACHE

AC_CONFIG_HEADERS([config.h])

AM_PROG_AR
LT_INIT([shared disable-static pic-only])
AC_SUBST([LIBTOOL_DEPS])

CFLAGS="-O3 -g -D_GNU_SOURCE -Wall -Wpedantic -Werror-implicit-function-declaration"

# Dependencies
PKG_PROG_PKG_CONFIG
PKG_INSTALLDIR

# Checks for libraries.
AX_PTHREAD
AC_CHECK_LIBNUMA

# Set C11 Std
AC_CHECK_C11

AC_PROG_AWK
AC_PROG_CC
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

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_SIZE_T
AC_C_RESTRICT

AC_CONFIG_FILES([Makefile nos-v.pc])
AC_OUTPUT

_AS_ECHO([])
_AS_ECHO([])
_AS_ECHO([Configuration summary:])
_AS_ECHO([   Compiler version... ${CC_VERSION}])
_AS_ECHO([   Compiler optimization flags... ${opt_CFLAGS}])
_AS_ECHO([   Compiler debug flags... ${DEBUG_CFLAGS}])
_AS_ECHO([])
