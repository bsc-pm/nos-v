# Coding Style

The coding style of nOS-V is formally defined in the `.clang-format` file distributed with the source.
We recommend using the [clang-format](https://clang.llvm.org/docs/ClangFormat.html) tool, which can be integrated with most text editors and IDEs.

TL;DR: Tab-only, 4-wide. C11 with K&R indentation.

This guide provides a more detailed overview of preferred and enforced coding practices, as well a some background rationale.
All files in nOS-V must follow the practices outlined below. If you see code not following this style guide, change it.
Any nOS-V related projects are invited to copy and use this guide.

## C Version and Compilers

This project uses C11 as its standard (which, for all intents and purposes, is equivalent to C17). The C compiler must support the `-std=c11` flag.

We officially support the following compilers:

 - GCC >= 5.4.0
 - clang >= 11.0.0

> For GCC, we stick to the oldest version we can reasonably support, because some HPC machines have really old software stacks.
> For clang, we stick to the version distributed in debian stable, as that should be old enough.

Some GNU extensions are allowed, but they should be wrapped through a generic definition in `compiler.h`, to facilitate portability if needed.
However, the following extensions are forbidden:
 - Variable-Length Arrays
 - `alloca` and its friends
 - The [Elvis Operator](https://en.wikipedia.org/wiki/Elvis_operator) `?:`
 - Nonlocal `goto`s
 - Nested functions
 - Anything that could be considered dark magic

## Code structure

All code should go inside the `src/` directory, and tests under the `tests/` directory.

Inside the `src/` directory, you should create subdirectories to group all files that do related things or form a component of the runtime. For example, all the source code related to memory management goes into `src/memory`.

Headers should be separated from the source, and go into `src/include`, which should have exactly the same directory structure as `src/`.
Note the following conventions:

 - Generic headers containing only constants or **re-usable** data structures go into `src/include/generic`.
 - Architecture-specific code should go into `src/include/generic/arch/[x86|arm64|power].h`
 - **Very** high-level or extremely common headers may go into `src/include` without any subdirectory.
 - The **public** nOS-V API headers should be placed in `api/`.

## Copyright

Every file in nOS-V should be licensed under the GNU GPLv3, and contain the standard copyright notice. Use multi-line comments for this purpose. A `.c` file that was created in 2020 and last edited in 2022 should have the following notice starting **in the first line**:

```c
/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2020-2022 Barcelona Supercomputing Center (BSC)
*/
```

## Headers

In general, each `.c` file will have an associated `.h` file in the same path but under `src/include`.

All headers should be self-contained (no special rules to include them, no hidden dependencies). This means that each header has the appropriate header guards, and includes **all** other headers it uses.

Make a best-effort to include only the headers that are *strictly necessary*, as it will reduce compilation time. However, do not use forward declarations just to reduce included headers. Use forward declarations only to break dependency cycles.

### Header guards

Every header must define a header guard, which allows including a header multiple times without redefining symbols. The format of the `#define` symbol should be `<FILE>_H`, but if this is going to create a non-unique guard, add part of the path.

For example, the file `src/include/generic/clock.h` has the following guard:

```c
#ifndef CLOCK_H
#define CLOCK_H

[...]

#endif // CLOCK_H
```

### Inline functions

Short or performance-critical functions are encouraged to be defined as `static inline` in the header file.

Avoid declaring exceedingly long functions as inline (+20 lines).

### Include ordering

Include headers in two groups: C system headers (first), and then internal headers (second), separated by a blank line. Each group should be ordered alphabetically. Use the following example as guidance:

```c
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "common.h"
#include "config/config.h"
#include "defaults.h"
#include "generic/clock.h"
#include "hardware/cpus.h"
#include "instr.h"
#include "memory/sharedmemory.h"
#include "nosv-internal.h"
#include "scheduler/scheduler.h"
```

## Variables, linkage, and storage classes

### Internal linkage

As nOS-V is a shared library, **only** functions and variables defined in the public API should be exported. Any other function should have internal linkage.

For functions that are only used inside a single `.c` file, declare them `static` or `static inline`.
Functions that are used in multiple places throughout the library should be declared in their header with the `__internal` specifier from the `compiler.h` header.

### Local variables

Place local variables in the narrowest scope possible, and prefer to initialize them in the declaration.

C11 allows mixing declarations and code, and we encourage declaring the variables in the most local scope possible, and close to their first use.

```c
int a;
a = 0; // BAD

int a = 0; // GOOD
```

Variables used only inside `for` and `while` statements should be declared within them.

```c
int i;
for (i = 0; i < N; ++i) // BAD

for (int i = 0; i < N; ++i) // GOOD
```

### Static and global variables
Static and global variables are perfectly acceptable if needed. However, remember to give them internal linkage.

## Functions

### Arguments
C functions have return values. If you have a function that returns only one thing, use its return value over output parameters. However, if you return multiple things, use output parameters instead of returning structs.

When ordering parameters, place any input parameters before output parameters. If you have to add a new parameter, re-order the existing ones if needed, don't just add it last.

Avoid input/output parameters when possible.

### Simpler is better
Write short and simple functions that do one logical thing.
There are no hard rules, but if you can't read your entire function from a screen, consider splitting it. If in doubt, ask yourself: "*What would Uncle Bob do?*".

## Naming
You should use names that make your code readable by **everyone**.
This means: be descriptive. Don't worry about horizontal space (use common sense). The time of the people that will have to review and read your code is far more expensive than drawing some more pixels.

That said, widely known abbreviations are perfectly fine, and loop counters should be single-character variables, as that is more readable.

Consider the following example:
```c
int cnt(int a[], int num_elems_in_a) /* Unespecific function name */
{
	int x = 0; // Indescriptive

	for (int current_element = 0; current_element < num_elems_in_a; ++current_element) /* Too long */
		x += (a[current_element] == 1);

	return x;
}
```

Versus a more readable alternative:
```c
int count_ones(int arr[], int n)
{
	int sum = 0;

	for (int i = 0; i < n; ++i)
		sum += (arr[i] == 1);

	return sum;
}
```

Most importantly, a programmer will understand immediately what a line reading the following means:
```c
int ones = count_ones(arr, n);
```

Without the need to read what `count_ones` does.

## Line length
There is no hard limit, but use common sense.

## Comments
You can use multi-line or single-line comments as you wish, but don't mix them.
If you use single-line comments, leave a space between the `//` and your first character, and capitalize it.
Don't use doxygen-style comments (`//!`).

Do this:
```
// This is a wonderful comment
int a = b;
```

Not this:
```
//this is a terrible comment
int a = b;
```

Public APIs should have comments over each function explaining what it does and if it has any restrictions.
Otherwise, there are no enforced comment locations.
However, comments are strongly encouraged everywhere it would increase readability or provide insight at why
a piece of code was written that way.

## Indentation
Indentation uses 4-wide tabs. Trailing whitespaces are forbidden.

You must follow [K&R style](https://en.wikipedia.org/wiki/Indentation_style#K&R_style).

Don't put multiple statements on a single line.
```
// GOOD
if (a)
	b();

// BAD
if (a) b();
```

Don't use comma operators to forego braces.
```
// GOOD
if (a) {
	b();
	c();
}

// BAD
if (a)
	b(), c();
```

## Assertions and error-checking
Check for errors as much as possible.

 - Make liberal use of `assert()`, but only to check for things that **should never happen**.
 - Use error checking + `nosv_abort()` to check for things that **could happen, but are fatal**.
 - Use warnings (`nosv_warn()`) to check for things that **could happen, but are not fatal**.
 - Use error codes for all public APIs. Don't crash just because a user supplied invalid input!
