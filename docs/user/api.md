# nOS-V API

The nOS-V API follows the same convention as most other C APIs:

 - Every function that is not a getter/setter returns an `int`.
The return value is `0` on success and some negative error code in any other case, such as defined in [Error Handling](errors.md).
Checking the return value of every nOS-V call is *strongly encouraged*.

 - Getters return directly the value of the field they are getting.
Setters return `void`.

## Types

The basic types in the API are the following:

### `nosv_flags_t`

Represents a field that can contain multiple flags, which can be combined together.
It is used as an argument for many calls which accept a flags argument.
Can be directly initialized in user code. Its size is defined as 8 bytes.

```c
nosv_flags_t flags = (NOSV_SUBMIT_UNLOCKED | NOSV_SUBMIT_IMMEDIATE);
int res = nosv_submit(..., flags);
```

### `nosv_task_t`

Represents a nOS-V task. Is created by `nosv_create` or `nosv_attach` and destroyed by `nosv_destroy` or `nosv_detach`.

Each task has an associated task type which specifies how each task is executed.

Internally, `nosv_task_t` is a pointer to `struct nosv_task`, but user programs must not rely in this
detail.

### `nosv_task_type_t`

Represents a task type, which can be associated to *n* tasks.
Is created by `nosv_type_init` and destroyed by `nosv_type_destroy`.

It contains information relevant to how tasks of this type are executed.

Internally, `nosv_task_type_t` is a pointer to `struct nosv_task_type`, but user programs must not rely in this
detail.

### `nosv_affinity_t`

Represents preferred or strict affinity to a specific CPU core or NUMA domain.
Is created by `nosv_affinity_get`, and does not need to be destroyed.

## Methods

### Initialization and shutdown
```c
int nosv_init(void);

int nosv_shutdown(void);
```

Before executing any other nOS-V method, the user must call `nosv_init` to initialize the runtime. Similarly, before ending the program, the user must call `nosv_shutdown` to clean up the runtime. No other calls to nOS-V can be performed after shutdown.

### Task types
```c
int nosv_type_init(
	nosv_task_type_t *type /* out */,
	nosv_task_run_callback_t run_callback,
	nosv_task_end_callback_t end_callback,
	nosv_task_completed_callback_t completed_callback,
	const char *label,
	void *metadata,
	nosv_cost_function_t cost_function,
	nosv_flags_t flags);

int nosv_type_destroy(
	nosv_task_type_t type,
	nosv_flags_t flags);
```

Task types must be created using the `nosv_type_init` function, and destroyed using `nosv_type_destroy`.

Types define how each task is run and completed by the nOS-V runtime. To that end, users must define a `run_callback`,
which will be called once the task is scheduled for execution, and users can optionally define an `end_callback`, called
once the task has finished running, and a `completed_callback`, called after both other callbacks have run and no events
are pending for the task. It is safe to call `nosv_destroy` from a `completed_callback`. All callbacks must return `void`
and have a single `nosv_task_t` argument, which corresponds to the current task.

Types may also define a label, which will be used only for instrumentation purposes, or leave it `NULL`. It is safe to
dispose of the string used for the `label` argument after type creation, since the string is copied.

Types may define a `metadata` argument, which is a single `void *` which will be stored into the types metadata and can be
retrieved later though the `nosv_get_task_type_metadata` call.

Types may define a `cost_function`, which is used by the monitoring subsystem of nOS-V to normalize task performance counters
depending on the amount of work. A cost function accepts a single `nosv_task_t` argument, corresponding to the task for which
cost has to be obtained, and must return a `uint64_t` value, being a numerical proxy for the task's computational cost.

### Managing tasks
```c
int nosv_create(
	nosv_task_t *task /* out */,
	nosv_task_type_t type,
	size_t metadata_size,
	nosv_flags_t flags);

int nosv_destroy(
	nosv_task_t task,
	nosv_flags_t flags);
```

Tasks must be created and destroyed through the `nosv_create` and `nosv_destroy` calls. Task creation must assign a valid
task type to the created task, and can optionally request space inside of the allocated task descriptor to store custom
metadata, specified through the `metadata_size` argument. The pointer to the allocated space can then be requested through
the `nosv_get_task_metadata` call. The requested `metadata_size` must be lower or equal to `NOSV_MAX_METADATA_SIZE`, otherwise
the call will fail.

Users may use the `NOSV_CREATE_PARALLEL` flag to indicate the task to be created is a parallel task.

Once a task descriptor has been destroyed through the `nosv_destroy` call, any later calls to the nOS-V API or references to
the task's metadata are undefined behaviour.

```c
int nosv_submit(
	nosv_task_t task,
	nosv_flags_t flags);
```

Tasks are marked as ready to execute and submitted to the nOS-V scheduler through the `nosv_submit` call. This call can also be
used to unblock any paused tasks (through the `nosv_pause` call), or to perform and early wakeup of a task blocked through
`nosv_waitfor`.

If no flags are specified (`NOSV_SUBMIT_NONE`), the task is submitted to the nOS-V scheduler for later execution through
any free worker threads. The available flags have specific behaviour:
 * `NOSV_SUBMIT_BLOCKING`: blocks the task calling `nosv_submit` until the submitted task has completed. Must be used from a task context.
 * `NOSV_SUBMIT_IMMEDIATE`: hint nOS-V to execute this task in the same CPU where the current task is running, immediately after finishing or pausing it. Must be used from a task context.
 * `NOSV_SUBMIT_INLINE`: execute the submitted task inline, in the same stack frame as the caller thread, which will not resume until the task ends.
 * `NOSV_SUBMIT_DEADLINE_WAKE`: wake up a waitfor task even if its timeout has not expired yet.

Tasks can be re-submitted once they have ended, but re-submitting a task before it has ended, if that task hasn't called or isn't going to call `nosv_pause` is undefined behaviour.

```c
int nosv_attach(
	nosv_task_t *task /* out */,
	nosv_affinity_t *affinity,
	const char *label,
	nosv_flags_t flags);

int nosv_detach(
	nosv_flags_t flags);
```

The `nosv_attach` and `nosv_detach` calls allow turning a currently running pthread into a nOS-V task. When calling `nosv_attach` from
outside a nOS-V task context, the calling thread will block and be assigned to a new nOS-V task, which will be submitted for execution.
When scheduled, the `nosv_attach` call will return and the caller thread will resume, but this time from inside a nOS-V task context.

This operation can be reversed using `nosv_detach`.

### Getting the current nOS-V task
```c
nosv_task_t nosv_self(void);
```

A call to `nosv_self` will return the currently executing nOS-V task, or `NULL` if called outside of nOS-V's task execution
context.

### Blocking and yielding tasks
```c
int nosv_pause(
	nosv_flags_t flags);

int nosv_waitfor(
	uint64_t target_ns,
	uint64_t *actual_ns /* out */);

int nosv_yield(
	nosv_flags_t flags);

int nosv_schedpoint(
	nosv_flags_t flags);
```

### Event API
```c
int nosv_increase_event_counter(
	uint64_t increment);

int nosv_decrease_event_counter(
	uint64_t increment);
```

### Synchronization API

The nosv mutex API is similar to the pthread mutex API; the difference is in
the contended behaviour. When a `nosv_mutex_lock()` call cannot acquire the
lock, the current task is paused and another task is scheduled in the current
core. Instead, a contended `pthread_mutex_lock()` would block the current nosv
worker thread without nosv noticing, which would lead to an idle core incapable
of running tasks until the `pthread_mutex_lock()` call returns.

```c
int nosv_mutex_init(
	nosv_mutex_t *mutex,
	nosv_flags_t flags);

int nosv_mutex_destroy(
	nosv_mutex_t mutex);

int nosv_mutex_lock(
	nosv_mutex_t mutex);

int nosv_mutex_trylock(
	nosv_mutex_t mutex);

int nosv_mutex_unlock(
	nosv_mutex_t mutex);
```

The nosv barrier API behaves as the pthread barrier, but with the same
contention mechanics as the nosv mutex.

```c
int nosv_barrier_init(
	nosv_barrier_t *barrier,
	nosv_flags_t flags,
	unsigned count);

int nosv_barrier_destroy(
	nosv_barrier_t barrier);

int nosv_barrier_wait(
	nosv_barrier_t barrier);
```

The nosv condition variable API behaves as the pthread condition variable, but
with the same contention mechanics as the nosv mutex.

```c
int nosv_cond_init(
    nosv_cond_t *cond,
    nosv_flags_t flags);

int nosv_cond_destroy(
	nosv_cond_t cond);

int nosv_cond_signal(nosv_cond_t cond);

int nosv_cond_broadcast(nosv_cond_t cond);

int nosv_cond_wait(
    nosv_cond_t cond,
    nosv_mutex_t mutex);

int nosv_cond_timedwait(
    nosv_cond_t cond,
    nosv_mutex_t mutex,
    const struct timespec *abstime);
```

### Helper functions
```c
int nosv_get_num_cpus(void);

int nosv_get_current_logical_cpu(void);

int nosv_get_current_system_cpu(void);
```

### Task getters and setters
```c
void *nosv_get_task_metadata(nosv_task_t task);
nosv_task_type_t nosv_get_task_type(nosv_task_t task);
int nosv_get_task_priority(nosv_task_t task);
void nosv_set_task_priority(nosv_task_t task, int priority);
```

### Task type getters and setters
```c
nosv_task_run_callback_t nosv_get_task_type_run_callback(nosv_task_type_t type);
nosv_task_end_callback_t nosv_get_task_type_end_callback(nosv_task_type_t type);
nosv_task_completed_callback_t nosv_get_task_type_completed_callback(nosv_task_type_t type);
const char *nosv_get_task_type_label(nosv_task_type_t type);
void *nosv_get_task_type_metadata(nosv_task_type_t type);
```

### Batch Submit API
```c
int nosv_set_submit_window_size(size_t submit_window_size);
int nosv_flush_submit_window(void);
```

### Suspend API

The suspend API allows the finalization of the task body without completing the task. If a task calls `nosv_suspend()` during the execution of its body, when the body ends, the task will be suspended instead of completed. In essence, this means that the nOS-V runtime will assume its body hasn't finished yet. When the task is resumed, however, **it is not guaranteed to be on the same pthread**.

Combined with an external state, such as stackless coroutines, the suspension mechanism can replicate the behavior of the blocking API without pausing the thread.

By default, a suspended task will only be rescheduled to complete its body if it is re-submitted through `nosv_submit`. However, this behavior can be changed through the `nosv_set_suspend_mode` call. Four suspend modes are provided, with different behaviors:
 - `NOSV_SUSPEND_MODE_NONE`: Default behavior
 - `NOSV_SUSPEND_MODE_SUBMIT`: Submits the task immediately upon suspension. It can be used to yield execution to other ready tasks. If `args` = 1, it will behave like a `nosv_yield()` call, where the task is not rescheduled until all remaining tasks have been.
 - `NOSV_SUSPEND_MODE_TIMEOUT_SUBMIT`: Submits the task with a specific deadline upon suspension. The deadline can be specified in ns through the `args` argument.
 - `NOSV_SUSPEND_MODE_EVENT_SUBMIT`: Submits the task when it has no remaining events. This is used in combination with the event API, and it allows suspending a task until every event has occurred, at which point it will be resubmitted by nOS-V.

```c
int nosv_set_suspend_mode(
	nosv_suspend_mode_t suspend_mode,
	uint64_t args);

int nosv_suspend(void);
```
