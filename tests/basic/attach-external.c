#include "test.h"

#include <nosv.h>
#include <pthread.h>
#include <sched.h>

#ifdef ENABLE_INSTRUMENTATION
// Load instrumentation definitions from nosv header for now
#include "instr.h"
#endif

static nosv_task_type_t type;
static test_t test;


static void *start_routine(void *arg)
{
	nosv_task_t task;

#ifdef ENABLE_INSTRUMENTATION
	// The thread must be instrumented *outside* nosv, before it
	// calls any external nosv function which may emit events.
	instr_thread_init();
	instr_thread_execute(-1, -1, 0);
#endif
	nosv_attach(&task, type, 0, NULL, NOSV_ATTACH_NONE);
	test_ok(&test, "External thread can attach");

	nosv_detach(NOSV_DETACH_NONE);
	test_ok(&test, "External thread can detach");

#ifdef ENABLE_INSTRUMENTATION
	// The thread end also flushes the events to disk.
	instr_thread_end();
#endif
	return NULL;
}

int main()
{
	test_init(&test, 4);

	nosv_init();

	nosv_type_init(&type, NULL, NULL, NULL, "main", NULL, NULL, NOSV_TYPE_INIT_EXTERNAL);

	pthread_t external_thread;
	pthread_create(&external_thread, NULL, &start_routine, NULL);

	nosv_task_t task;
	nosv_attach(&task, type, 0, NULL, NOSV_ATTACH_NONE);
	test_ok(&test, "Main thread can attach");
	nosv_detach(NOSV_DETACH_NONE);
	test_ok(&test, "Main thread can detach");

	pthread_join(external_thread, NULL);

	nosv_type_destroy(type, NOSV_DESTROY_NONE);
	nosv_shutdown();
}
