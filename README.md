# nOS-V Library

Nanos6 is a runtime that implements the nOS-V tasking API,
developed by the [*Programming Models group*](https://pm.bsc.es/)
at the [**Barcelona Supercomputing Center**](http://www.bsc.es/).

## Licensing

The nOS-V Library is Free Software, licensed under the clauses of the
GNU GPL v3 License included in the [COPYING](COPYING) file.
The copyright of the files included in this package belongs to the
Barcelona Supercomputing Center, unless otherwise stated.

## Installation

### Build requirements

The following software is required to build and install nOS-V:

1. automake, autoconf, libtool, pkg-config, make and a C11 compiler
1. [numactl](http://oss.sgi.com/projects/libnuma/)

### Optional dependencies

The following software is required to enable optional features:

(None)

### Build procedure

When cloning from a repository, the building environment must be prepared through the following command:

```sh
$ autoreconf -f -i -v
```

When the code is distributed through a tarball, it usually does not need that command.

Then execute the following commands:

```sh
$ ./configure --prefix=INSTALLATION_PREFIX ...other options...
$ make all check
$ make install
```

where `INSTALLATION_PREFIX` is the directory into which to install nOS-V.

## Contributing

The development of nOS-V is based on a few simple principles that are maintained throughout the library:

1. No dependencies, aside from libc and pthreads
1. C11 with K&R C indentation. There is a ".clang-format" file provided
1. Simple is better. Stablishing compile-time limits is encouraged if it simplifies significantly the code
