# Configuration

The behaviour of nOS-V can be tuned through the use of its configuration options. These options are specified in the configuration file using [TOML 1.0](https://toml.io/en/v1.0.0) syntax. A default configuration file is included in `$PREFIX/share/nosv.toml`, and can be used as a base for user-defined configuration files. All possible options and its effects are explained on the default file.

Moreover, individual configuration options can be overriden through the use of the `NOSV_CONFIG_OVERRIDE` environment variable. Multiple comma-separated options can be specified using this method.

On startup, nOS-V will find and parse **one** configuration file. nOS-V will search for this configuration file in the following order of preference:

1. The file specified in the `NOSV_CONFIG` environment variable
1. A file named `nosv.toml` present in the current directory
1. The default config file located in `$PREFIX/share/nosv.toml`

After this file has been loaded, individual overrides from `NOSV_CONFIG_OVERRIDE` will be applied.

Consider the following example where we specify the `~/nosv.toml` file and then override two configuration variables:
```bash
NOSV_CONFIG="~/nosv.toml" NOSV_CONFIG_OVERRIDE="turbo.enabled=true,hwcounters.backend=papi" ./program
```

## Presets

Presets are a mechanism to override several configuration options that are needed for common use-cases. For example, there is a preset for running completely isolated nOS-V applications, and another to run MPI applications. These presets will apply a specific set of configurations that are needed to support the specific use-case.

Users can apply a preset by using the `NOSV_PRESET` environment variable. For example, the `isolate` preset can be applied by setting `NOSV_PRESET=isolate`

The following presets are currently available:
| Preset  | Effect |
| --- | --- |
| `isolate` | Runs nOS-V applications without sharing the nOS-V instances. CPU binding is inherited from the parent process |
| `mpi` | Runs applications sharing a single nOS-V instance amongst processes of the same user. CPU binding is set to the whole machine, and default affinity to the parent process' binding. |
