#!/bin/bash
#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021-2022 Barcelona Supercomputing Center (BSC)

set -e

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
find $SCRIPT_DIR/../src $SCRIPT_DIR/../tests -name "*.[ch]" -print0 | \
while IFS= read -rd '' f;
do
	# This may miss some instances, but we:
	# Find non-indented non-define non-comment lines that have one or more words
	# followed by an space and another word, and then an empty ()
	# Finally we filter the tests main() functions, as those are actually correct
	# instances of undefined parameter numbers
	candidates=$(grep -nP '^[^\t^#^\/][^\=]+ [^\b]+\(\)' $f | grep -v 'main()' -- || true)
	if [ -n "$candidates" ];
	then
		echo "In file ${f}:"
		echo "${candidates}"
		echo ""
	fi
done

