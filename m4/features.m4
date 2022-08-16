#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2022 Barcelona Supercomputing Center (BSC)

AC_DEFUN([AC_NOSV_FEATURES],
	[
		AC_ARG_ENABLE(
			[priority],
			AS_HELP_STRING([--enable-priority],
				[enable experimental support for task priorities in the nOS-V scheduler @<:@default=no@:>@]),
			[], [enable_priority=no]
		)

		case "${enable_priority}" in
			yes) AC_DEFINE_UNQUOTED([ENABLE_PRIORITY], [1], [Enable support for task priority]) ;;
			no) AC_DEFINE_UNQUOTED([ENABLE_PRIORITY], [0], [Disable support for task priority]) ;;
			*) AC_MSG_ERROR([Unknown value ${enable_priority} for --enable-priority]) ;;
		esac
	]
)
