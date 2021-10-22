#!/bin/bash
#	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.
#
#	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
#
#	Check that every file in nOS-V has final whitespaces

set -e

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
find $SCRIPT_DIR/../src $SCRIPT_DIR/../tests -name *.[ch] -print0 | while IFS= read -rd '' f; do tail -c1 < "$f" | read -r _ || echo >> "$f"; done
