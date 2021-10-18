#include "test.h"

#include <nosv.h>
#include <nosv/affinity.h>
#include <sched.h>
#include <stdatomic.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define NITERATIONS 50
#define NTASKS 4

volatile int iteration = 0;
volatile int completed = 0;

nosv_task_t tasks[NTASKS];

test_t test;

int get_first_available_cpu(void)
{
	cpu_set_t set;
	sched_getaffinity(0, sizeof(set), &set);

	for (int c = 0; c < CPU_SETSIZE; ++c) {
		if (CPU_ISSET(c, &set)) {
			return c;
		}
	}

	return -1;
}

void task_run(nosv_task_t task)
{
	const int time_us = 200;
	const int turn = (iteration % NTASKS);

	// Increase the iterations in a interleaved way
	while (iteration < NITERATIONS) {
		++iteration;

		do {
			// Yield the CPU if the quantum has been exceeded
			nosv_schedpoint(NOSV_SCHEDPOINT_NONE);

			// Consume quantum without wasting resources
			usleep(time_us);
		} while (iteration % NTASKS != turn && iteration < NITERATIONS);
	}

	test_ok(&test, "Task executed all iterations");
}

void task_comp(nosv_task_t task)
{
	atomic_fetch_add(&completed, 1);
}

int main() {
	test_init(&test, NTASKS);

	nosv_init();

	nosv_task_type_t task_type;
	nosv_type_init(&task_type, task_run, NULL, task_comp, "task", NULL, NOSV_TYPE_INIT_NONE);

	for (int t = 0; t < NTASKS; ++t)
		nosv_create(&tasks[t], task_type, 0, NOSV_CREATE_NONE);

	// Parallel tests should be using the same CPU
	int cpu = get_first_available_cpu();
	assert(cpu >= 0);

	nosv_affinity_t affinity = nosv_affinity_get(cpu, CPU, STRICT);

	for (int t = 0; t < NTASKS; ++t) {
		nosv_set_task_affinity(tasks[t], &affinity);
		nosv_submit(tasks[t], NOSV_SUBMIT_NONE);
	}

	while (atomic_load(&completed) != NTASKS)
		usleep(1000);

	for (int t = 0; t < NTASKS; ++t)
		nosv_destroy(tasks[t], NOSV_DESTROY_NONE);

	nosv_type_destroy(task_type, NOSV_TYPE_DESTROY_NONE);

	nosv_shutdown();

	return 0;
}
