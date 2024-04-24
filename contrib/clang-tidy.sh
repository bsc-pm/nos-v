#!/bin/bash
#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
find $SCRIPT_DIR/../src -name "*.[c]" -print0 | \
while IFS= read -rd '' f;
do
	clang-tidy $f --config-file="${SCRIPT_DIR}/clang-tidy-config" \
		-- -DHAVE_CONFIG_H -I.  -I${SCRIPT_DIR}/../src/include -I${SCRIPT_DIR}/../api -DINSTALLED_CONFIG_DIR="\"\"" -pthread -D_GNU_SOURCE -Wall -Wpedantic -Werror-implicit-function-declaration -g -std=c11
done
