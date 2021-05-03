#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)

AC_DEFUN([AC_CHECK_C11],
	[
        AX_CHECK_COMPILE_FLAG([-std=c11], [
            CFLAGS+=" -std=c11"
        ], [
            echo "There is no C compiler compatible with C11"
            exit -1
        ])
	]
)
