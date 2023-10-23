# nOS-V Library

nOS-V is a runtime library that implements the nOS-V tasking API,
developed by the [*Programming Models group*](https://pm.bsc.es/)
at the [**Barcelona Supercomputing Center**](http://www.bsc.es/).

Its main goal is to provide a low-level and low-overhead tasking
runtime which supports co-execution and can be leveraged by higher-level
programming models.

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

1. [ovni](https://github.com/bsc-pm/ovni) to generate execution traces for performance analysis with [Paraver](https://tools.bsc.es/paraver)
1. [PAPI](http://icl.utk.edu/papi/software/) for gathering performance counters (version >= 5.6.0)

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

The configure script accepts several options:

1. `--enable-debug`: Add compiler debug flags and enable additional internal debugging mechanisms
1. `--enable-asan`: Add compiler flags to enable address sanitizing
1. `--with-ovni`: Enable ovni instrumentation and specify the ovni library installation prefix
1. `--with-papi`: Enable PAPI counters and specify the PAPI library installation prefix
1. `--with-libnuma`: Specify the numactl library installation prefix

### Execution requirements

nOS-V must come first in the Lookup Scope of the executable. This is needed because nOS-V provides a number of interceptor functions. To do so, if you link nOS-V directly to your executable, link nOS-V before any other shared library. If linking nOS-V in a shared library, you will either need to link nOS-V again in your main executable or preload nOS-V using the `LD_PRELOAD` environment variable.

Link-time example:

```sh
### wrong
$ gcc main.c -lmylib -lnosv
### correct
$ gcc main.c -lnosv -lmylib
```

Run-time example:

```sh
$ LD_PRELOAD=<nosv_install_path>/lib/libnosv.so ./main
```

## Usage and documentation

The user documentation for nOS-V can be found [here](docs/index.md)

## Contributing

The development of nOS-V is based on a few simple principles that are maintained throughout the library:

1. No dependencies, aside from libc and pthreads
1. C11 with K&R C indentation. There is a ".clang-format" file provided
1. Simple is better. Stablishing compile-time limits is encouraged if it simplifies significantly the code

## Citing

If you use nOS-V in your research, we ask that you cite the following [preprint](https://arxiv.org/abs/2204.10768):

```
@misc{alvarez2022nosv,
      title={nOS-V: Co-Executing HPC Applications Using System-Wide Task Scheduling},
      author={David Álvarez and Kevin Sala and Vicenç Beltran},
      year={2022},
      eprint={2204.10768},
      archivePrefix={arXiv},
      primaryClass={cs.DC}
}
```
