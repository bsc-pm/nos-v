#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)

AC_DEFUN([AC_CHECK_OVNI],
	[
		AC_ARG_WITH(
			[ovni],
			[AS_HELP_STRING([--with-ovni=prefix], [specify the installation prefix of the ovni library (required for instrumentation)])],
			[ ac_use_ovni_prefix="${withval}" ],
			[ ac_use_ovni_prefix="" ]
		)

		AC_MSG_CHECKING([the ovni installation prefix])
		if test x"${ac_use_ovni_prefix}" != x"" ; then
			AC_MSG_RESULT([${ac_use_ovni_prefix}])
			ovni_LIBS="-L${ac_use_ovni_prefix}/lib"
			ovni_CPPFLAGS="-I${ac_use_ovni_prefix}/include -DENABLE_INSTRUMENTATION"

			ac_save_CPPFLAGS="${CPPFLAGS}"
			ac_save_LIBS="${LIBS}"

			CPPFLAGS="${CPPFLAGS} ${ovni_CPPFLAGS}"
			LIBS="${LIBS} ${ovni_LIBS}"

			AC_CHECK_HEADERS([ovni.h], [], [AC_MSG_ERROR([ovni ovni.h header file not found])])
			AC_CHECK_LIB([ovni],
				[ovni_proc_init],
				[ovni_LIBS="${ovni_LIBS} -lovni -Wl,-rpath=${ac_use_ovni_prefix}/lib"],
				[AC_MSG_ERROR([ovni cannot be found])],
				[${ac_save_LIBS}]
			)

			CPPFLAGS="${ac_save_CPPFLAGS}"
			LIBS="${ac_save_LIBS}"
		else
			AC_MSG_RESULT([not enabled])
			ovni_LIBS=""
			ovni_CPPFLAGS=""
		fi

		AC_SUBST([ovni_LIBS])
		AC_SUBST([ovni_CPPFLAGS])
	]
)

