# Tracing

To figure out why a particular application or set of applications are not delivering the desired performance sometimes
it is useful to extract and examine execution traces. These traces have much more granular information than what a
traditional profiler can provide, and thus much more complex analysis can be carried out.

nOS-V has the capability to generate lightweight binary traces through the [ovni](https://ovni.readthedocs.io) library.
Furthermore, traces from several nOS-V enabled applications, or even distributed nOS-V applications can be combined
to generate a single coherent trace. Some libraries based in nOS-V (like the NODES runtime, for example) can also
generate additional events which will be combined with the nOS-V trace.

To enable ovni tracing, nOS-V must be built with the `--with-ovni` option pointing to a valid ovni installation. Then,
the ovni instrumentation must be enabled through the `instrumentation.version=ovni` configuration option while executing
the target application. The resulting trace will be left in the `ovni/` directory, and then transformed into a Paraver
trace using the `ovniemu` utility. The Paraver configuration files (views) can be found in the `ovni/cfg` directory.

See the [ovni documentation](https://ovni.readthedocs.io) for more details.

The level of detail can be controlled with the `ovni.level` configuration option, which can be a number between 0 and 4
(both included). Higher levels will emit more events, thus containing more detail but increasing significantly the size
of the traces (and the overhead incurred).

If more fine-grained control is needed, the `ovni.events` configuration option contains a list of event categories to be
emitted. Moreover, prefixing the category with a `!` symbol will disable that specific event category. The following
table contains a full list of event categories and which level are they included on, as well as which events will be emitted when active.

| Category         | Level | Description |
| ---------------- | ----- | ----------- |
| basic            |     0 | Thread and process creation / destruction and blocking, needed for other instrumentation levels |
| worker           |     1 | Worker thread start and stop |
| task             |     1 | Task creation/destruction, start and end |
| scheduler        |     2 | Scheduler task obtention and serving |
| scheduler_hungry |     2 | Signals when threads enter the scheduler and are looking for work (hungry) or cease to be looking for work |
| scheduler_submit |     2 | Scheduler task submission |
| api_attach       |     2 | Calls to `nosv_attach` and `nosv_detach` |
| api_create       |     3 | Calls to `nosv_create` |
| api_destroy      |     3 | Calls to `nosv_destroy` |
| api_submit       |     3 | Calls to `nosv_submit` |
| api_pause        |     3 | Calls to `nosv_pause` |
| api_yield        |     3 | Calls to `nosv_yield` |
| api_waitfor      |     3 | Calls to `nosv_waitfor` |
| api_schedpoint   |     3 | Calls to `nosv_schedpoint` |
| api_mutex_lock   |     3 | Calls to `nosv_mutex_lock` |
| api_mutex_trylock|     3 | Calls to `nosv_mutex_trylock` |
| api_mutex_unlock |     3 | Calls to `nosv_mutex_unlock` |
| api_barrier_wait |     3 | Calls to `nosv_barrier_wait` |
| kernel           |     3 | Linux thread preemption events |
| breakdown        |     3 | Breakdown events |
| memory           |     4 | Calls to the internal memory allocator |
