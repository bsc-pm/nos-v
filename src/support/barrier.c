#include <stdbool.h>
#include <stdlib.h>

#include "common.h"
#include "instr.h"
#include "nosv.h"
#include "nosv-internal.h"
#include "generic/list.h"
#include "generic/spinlock.h"
#include "hardware/threads.h"


struct nosv_barrier {
	list_head_t list;
	nosv_spinlock_t lock;
	unsigned count;
	unsigned towait;
};


int nosv_barrier_init(
	nosv_barrier_t *barrier,
	nosv_flags_t flags,
	unsigned count)
{
	nosv_barrier_t bptr;

	if (flags & ~NOSV_BARRIER_NONE)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!count)
		return NOSV_ERR_INVALID_PARAMETER;

	// Initialize mutex object
	bptr = malloc(sizeof(*bptr));
	if (!bptr)
		return NOSV_ERR_OUT_OF_MEMORY;
	list_init(&bptr->list);
	nosv_spin_init(&bptr->lock);
	bptr->count = count;
	bptr->towait = count;
	*barrier = bptr;
	return NOSV_SUCCESS;
}

int nosv_barrier_destroy(nosv_barrier_t barrier)
{
	if (!barrier)
		return NOSV_ERR_INVALID_PARAMETER;
	free(barrier);
	return NOSV_SUCCESS;
}

int nosv_barrier_wait(nosv_barrier_t barrier)
{
	nosv_task_t current_task = worker_current_task();
	nosv_task_t task;
	list_head_t head;
	list_head_t *elem;

	if (!barrier)
		return NOSV_ERR_INVALID_PARAMETER;

	if (!current_task)
		return NOSV_ERR_OUTSIDE_TASK;

	instr_barrier_wait_enter();

	nosv_spin_lock(&barrier->lock);
	barrier->towait--;
	if (barrier->towait) {
		// We are not the last one
		// Wait for the other tasks
		list_add_tail(&barrier->list, &current_task->list_hook);
		nosv_spin_unlock(&barrier->lock);
		nosv_pause(NOSV_PAUSE_NONE);
		// We have been unblocked, we can return immediately
	} else {
		// We are the last task
		// First, restore the internal barrier state
		head = barrier->list;
		list_init(&barrier->list);
		barrier->towait = barrier->count;
		nosv_spin_unlock(&barrier->lock);
		// After this point, the barrier is safe to be reused

		// Second, wake up all waiting tasks. All tasks to be awaken are
		// linked between them. By submitting the first one, the batch
		// scheduler will add them all at once.

		// If count=1, there will be no waiting tasks
		if ((elem = list_front(&head))) {
			task = list_elem(elem, struct nosv_task, list_hook);
			nosv_submit(task, NOSV_SUBMIT_UNLOCKED);
		}
	}

	instr_barrier_wait_exit();

	return NOSV_SUCCESS;
}
