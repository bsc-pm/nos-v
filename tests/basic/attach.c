#include "test.h"

#include <nosv.h>
#include <sched.h>

int main() {
	test_t test;

	test_init(&test, 2);

	nosv_init();

	cpu_set_t original, attached, new;

	sched_getaffinity(0, sizeof(cpu_set_t), &original);

	nosv_task_type_t type;
	nosv_type_init(&type, NULL, NULL, NULL, "main", NULL, NOSV_TYPE_INIT_EXTERNAL);
	nosv_task_t task;
	nosv_attach(&task, type, 0, NULL, NOSV_ATTACH_NONE);
	nosv_detach(NOSV_DETACH_NONE);

	sched_getaffinity(0, sizeof(cpu_set_t), &new);
	test_check(&test, CPU_EQUAL(&original, &new), "nosv_detach restores the original thread affinity");

	nosv_attach(&task, type, 0, NULL, NOSV_ATTACH_NONE);
	sched_getaffinity(0, sizeof(cpu_set_t), &attached);
	nosv_detach(NOSV_DETACH_NO_RESTORE_AFFINITY);

	sched_getaffinity(0, sizeof(cpu_set_t), &new);
	test_check(&test, CPU_EQUAL(&attached, &new), "NOSV_DETACH_NO_RESTORE_AFFINITY skips restoring the original affinity");

	nosv_type_destroy(type, NOSV_DESTROY_NONE);
	nosv_shutdown();
}
