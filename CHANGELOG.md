# nOS-V Release Notes
All notable changes to this project will be documented in this file.

The format of this file is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

## Version [3.2.0], Fri Jun 6, 2025
This version corresponds to the OmpSs-2 2025.06 release. It is a minor release integrating fixes and improvements and some backward-compatible API extensions.

### Added
- Extended hwinfo API with new functionalities
- Updated compatibility to support ALPI v1.2
- Integrated code coverage

### Changed
- Reduced the number of instrumentation events triggered by `schedpoints` and `yields`
- Improved detection logic and internal representation of hardware information

### Fixed
- Fixed a TLS-related bug when nesting `attaches` via `fork()`
- Fixed synchronization issues involving `nosv_cond` mutexes
- Fixed memory consistency issues on certain architectures related to barriers in `complete_callbacks`
- Fixed an instrumentation bug where physical CPU IDs were incorrectly emitted instead of logical IDs, leading to emulator failures
- Minor code styling fixes


## Version [3.1.0], Fri Nov 15, 2024
This version corresponds to the OmpSs-2 2024.11 release. It contains performance and instrumentation improvements, support for RISC-V and coroutines, and a new topology API.

### Added
- Introduce support for breakdown model implementation, supported through the use of `ovniemu -b`.
- Introduce a Memory Pressure API, to query the current occupancy of the nOS-V shared memory segment.
- Refactor shutdown mechanism, using a coordinated approach to prevent contention during runtime shutdown.
- Allow re-initialization of nOS-V, permitting the call to `nosv_init()` after `nosv_shutdown()`.
- Add support for coroutines and similar constructs through the `nosv_suspend()` API.
- Add support for RISC-V.
- Introduce a Topology API, which allows the configuration of system topology through the nosv.toml file.
- Introduce `nosv_cond_t` and related calls, as a replacement for pthread condition variables.

### Changed
- Enable `turbo` setting by default, and add correctness checking to detect changes to FPU flags from outside of nOS-V.
- Allow submitting tasks as `NOSV_SUBMIT_IMMEDIATE` from a task's run callback.

### Fixed
- Miscellaneous fixes and improvements.


## Version [2.2.0], Wed May 15, 2024
This version corresponds to the OmpSs-2 2024.05 release. It contains batch task submission, a new barrier and mutex API and ovni instrumentation for parallel tasks.

### Added
- Implement a batch submission API, which can accumulate tasks to submit them in batch once a certain threshold has been reached.
- Add `nosv_mutex_t` and `nosv_barrier_t` as nOS-V aware alternatives to their pthread counterparts, improving interoperability.
- Add instrumentation points for the `nosv_attach` and `nosv_detach` calls.
- Add instrumentation for parallel tasks.
- Perform safety checks when the `turbo.enabled` configuration option is set to verify FPU flags are not modified by external libraries.
- Allow nOS-V programs to call `fork()` without leaving the forked process in an incoherent state.

### Changed
- Activate the `turbo.enabled` configuration option by default, which enables flush-to-zero in x86-64 and aarch64.
- Split instrumentation events for the scheduler to allow them to be more granularly controlled.
- Various refactors and bugfixes.


## Version [2.1.2], Fri Apr 12, 2024
This version is a bugfix release, containing only compatible fixes with the previous patch versions.

### Added
- Allow nested calls to `nosv_init` and `nosv_shutdown`
- Add compatibility with newer versions of the ovni library
- Bypass calls to the pthread library when using nOS-V fake affinity support

### Fixed
- Fix an assert that could trip under specific conditions when using parallel tasks


## Version [2.1.1], Fri Nov 17, 2023
This version corresponds to the OmpSs-2 2023.11 release. It contains changes to the attach API, support for the ALPI tasking interface, and support for parallel tasks.

### Added
- Add `misc.stack_size` config option to change the stack size of nOS-V threads
- Allow calling `nosv_init` and `nosv_shutdown` multiple times
- Implement `nosv_cancel` API to wake up blocked tasks before their timeout expires
- Add compatibility layer for calls to `sched_get/setaffinity` and `pthread_get/setaffinity`
- Implement `ovni.level` configuration option and fine-grained instrumentation control
- Add instrumentation points for the `nosv_create` and `nosv_destroy` APIs
- Implement parallel tasks which can be executed on multiple CPUs at once
- Implement ALPI (Async Lowlevel Programming Interface) support

### Changed
- Change error handling to return custom nOS-V error codes
- Change `nosv_attach` API to not require an explicit task type and support multiple attaches

### Fixed
- Various bugfixes and corrections

[3.2.0]: https://github.com/bsc-pm/nos-v/releases/tag/3.2.0
[3.1.0]: https://github.com/bsc-pm/nos-v/releases/tag/3.1.0
[2.2.0]: https://github.com/bsc-pm/nos-v/releases/tag/2.2.0
[2.1.2]: https://github.com/bsc-pm/nos-v/releases/tag/2.1.2
[2.1.1]: https://github.com/bsc-pm/nos-v/releases/tag/2.1.1
