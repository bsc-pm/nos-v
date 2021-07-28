/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2021 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <papi.h>
#include <pthread.h>
#include <stdlib.h>

#include "hwcounters/cpuhwcounters.h"
#include "hwcounters/taskhwcounters.h"
#include "hwcounters/threadhwcounters.h"
#include "hwcounters/papi/papicpuhwcounters.h"
#include "hwcounters/papi/papihwcounters.h"
#include "hwcounters/papi/papitaskhwcounters.h"
#include "hwcounters/papi/papithreadhwcounters.h"


__internal papi_backend_t backend;


void test_maximum_number_of_events()
{
	if (backend.verbose) {
		// TODO: print: "\n- Testing if the requested PAPI events are compatible..."
	}

	// Register the thread into PAPI and create an event set
	int ret = PAPI_register_thread();
	if (ret != PAPI_OK) {
		// TODO: fail: (ret, " when registering the main thread into PAPI - ", PAPI_strerror(ret));
	}

	int event_set = PAPI_NULL;
	ret = PAPI_create_eventset(&event_set);
	if (ret != PAPI_OK) {
		// TODO: fail: (ret, " when creating a PAPI event set for the main thread - ", PAPI_strerror(ret));
	}

	// After creating the event set and registering the main thread into PAPI
	// for the purpose of testing, test if all enabled events can co-exist
	for (size_t i = 0; i < HWC_PAPI_NUM_EVENTS; ++i) {
		// Try to add the event to the set
		int code = backend.enabled_event_codes[i];
		if (code != HWC_NULL_EVENT) {
			ret = PAPI_add_event(event_set, code);

			// If the event was added, log it. If it was not, before failing,
			// give information about the current incompatible event
			if (backend.verbose) {
				char code_name[PAPI_MAX_STR_LEN];
				int ret_name = PAPI_event_code_to_name(code, code_name);
				if (ret_name != PAPI_OK) {
					// TODO: fail: ret_name, " when converting from PAPI code to PAPI event name - ", PAPI_strerror(ret_name)
				}

				if (ret != PAPI_OK) {
					// TODO: print: ("  - Enabling ", code_name, ": FAIL");
				} else {
					// TODO: print: ("  - Enabling ", code_name, ": OK");
				}
			}

			// Regardless of the verbosity, if it failed, abort the execution
			if (ret != PAPI_OK) {
				// TODO: fail: "Cannot simultaneously enable all the requested PAPI events due to incompatibilities"
			}
		}
	}

	// Remove all the events from the EventSet, destroy it, and unregister the thread
	ret = PAPI_cleanup_eventset(event_set);
	if (ret != PAPI_OK) {
		// TODO: fail: ret, " when clearing the main thread's PAPI eventSet - ", PAPI_strerror(ret)
	}

	ret = PAPI_destroy_eventset(&event_set);
	if (ret != PAPI_OK) {
		// TODO: fail: ret, " when destorying the main thread's PAPI eventSet - ", PAPI_strerror(ret)
	}

	ret = PAPI_unregister_thread();
	if (ret != PAPI_OK) {
		// TODO: fail: ret, " when unregistering the main thread from the PAPI library - ", PAPI_strerror(ret)
	}
}

void papi_hwcounters_initialize(
	short verbose, short *num_enabled_counters,
	enum counters_t enabled_events[HWC_PAPI_NUM_EVENTS]
) {
	backend.enabled = 1;
	backend.verbose = verbose;
	backend.num_enabled_counters = 0;
	for (size_t i = 0; i < HWC_PAPI_NUM_EVENTS; ++i) {
		backend.id_table[i] = HWC_NULL_EVENT;
		backend.enabled_event_codes[i] = HWC_NULL_EVENT;
	}

	// Initialize the library
	int ret = PAPI_library_init(PAPI_VER_CURRENT);
	if (ret != PAPI_VER_CURRENT) {
		// TODO: Fail: (ret, " when initializing the PAPI library - ", PAPI_strerror(ret));
	}

	// Initialize the PAPI library for threads, and the domain
	ret = PAPI_thread_init(pthread_self);
	if (ret != PAPI_OK) {
		// TODO: Fail: (ret, " when initializing the PAPI library for threads - ", PAPI_strerror(ret));
	}

	ret = PAPI_set_domain(PAPI_DOM_USER);
	if (ret != PAPI_OK) {
		// TODO: Fail: (ret, " when setting the default PAPI domain to user only - ", PAPI_strerror(ret));
	}

	// Now test the availability of all the requested events
	if (verbose) {
		// TODO: print: "------------------------------------------------\n",
		// TODO: print: "- Testing requested PAPI events availabilities"
	}
	for (size_t id = HWC_PAPI_MIN_EVENT; id <= HWC_PAPI_MAX_EVENT; ++id) {
		short id_enabled = (short) enabled_events[id];
		if (id_enabled) {
			int code;
			ret = PAPI_event_name_to_code(counter_descriptions[id - HWC_PAPI_MIN_EVENT].descr, &code);
			if (ret != PAPI_OK) {
				// TODO: Fail: (ret, counter_descriptions[id - HWC_PAPI_MIN_EVENT].descr, " event not known by this version of PAPI - ", PAPI_strerror(ret));
			}

			ret = PAPI_query_event(code);
			if (verbose) {
				if (ret != PAPI_OK) {
					// TODO: print: ("  - ", counter_descriptions[id - HWC_PAPI_MIN_EVENT].descr, ": FAIL");
				} else {
					// TODO: print: ("  - ", counter_descriptions[id - HWC_PAPI_MIN_EVENT].descr, ": OK");
				}
			}

			if (ret != PAPI_OK) {
				// TODO: warn: (ret, " ", counter_descriptions[id - HWC_PAPI_MIN_EVENT].descr, " event unknown in this version of PAPI, skipping it - ", PAPI_strerror(ret));

				// Disable the event from the vector of enabled events
				enabled_events[id] = 0;
			} else {
				backend.enabled_event_codes[backend.num_enabled_counters] = code;
				backend.id_table[id - HWC_PAPI_MIN_EVENT] = backend.num_enabled_counters;
				backend.num_enabled_counters++;
			}
		}
	}

	if (!backend.num_enabled_counters) {
		// TODO: warn: ("No PAPI events enabled, disabling this backend");
		backend.enabled = 0;
	} else {
		*(num_enabled_counters) += backend.num_enabled_counters;
		backend.enabled = 1;

		// Test incompatibilities between PAPI events
		test_maximum_number_of_events();
	}

	if (verbose) {
		// TODO: print: "\n- Finished testing PAPI events availabilities\n",
		// TODO: print: "- Number of PAPI events enabled: ", backend.num_enabled_counters, "\n",
		// TODO: print: "------------------------------------------------"
	}
}

int papi_hwcounters_get_inner_identifier(enum counters_t type)
{
	assert((type - HWC_PAPI_MIN_EVENT) < HWC_PAPI_NUM_EVENTS);

	return backend.id_table[type - HWC_PAPI_MIN_EVENT];
}

short papi_hwcounters_counter_enabled(enum counters_t type)
{
	assert((type - HWC_PAPI_MIN_EVENT) < HWC_PAPI_NUM_EVENTS);

	return (backend.id_table[type - HWC_PAPI_MIN_EVENT] != HWC_NULL_EVENT);
}

size_t papi_hwcounters_get_num_enabled_counters()
{
	return backend.num_enabled_counters;
}

void papi_hwcounters_thread_initialize(papi_threadhwcounters_t *thread_counters)
{
	if (backend.enabled) {
		// Register the thread into PAPI and create an EventSet for it
		int ret = PAPI_register_thread();
		if (ret != PAPI_OK) {
			// TODO: Fail: ret, " when registering a new thread into PAPI - ", PAPI_strerror(ret)
		}

		int event_set = PAPI_NULL;
		ret = PAPI_create_eventset(&event_set);
		if (ret != PAPI_OK) {
			// TODO: Fail: (ret, " when creating a PAPI event set - ", PAPI_strerror(ret));
		}

		// Add all the enabled events to the EventSet
		ret = PAPI_add_events(event_set, backend.enabled_event_codes, backend.num_enabled_counters);
		if (ret != PAPI_OK) {
			// TODO: Fail: ret, " when initializing the PAPI event set of a new thread - ", PAPI_strerror(ret));
		}
		assert(event_set != PAPI_NULL);

		// Set the EventSet to the thread and start counting
		papi_threadhwcounters_set_eventset(thread_counters, event_set);
		ret = PAPI_start(event_set);
		if (ret != PAPI_OK) {
			// TODO: Fail: (ret, " when starting a PAPI event set - ", PAPI_strerror(ret));
		}
	}
}

void papi_hwcounters_thread_shutdown(__maybe_unused papi_threadhwcounters_t *counters)
{
	if (backend.enabled) {
		int ret = PAPI_unregister_thread();
		if (ret != PAPI_OK) {
			// TODO: Fail: (ret, " when unregistering a PAPI thread - ", PAPI_strerror(ret));
		}
	}
}

void papi_hwcounters_update_task_counters(
	papi_threadhwcounters_t *thread_counters,
	papi_taskhwcounters_t *task_counters
) {
	if (backend.enabled) {
		assert(thread_counters != NULL);
		assert(task_counters != NULL);

		int event_set = papi_threadhwcounters_get_eventset(thread_counters);
		papi_taskhwcounters_read_counters(task_counters, event_set);
	}
}

void papi_hwcounters_update_runtime_counters(
	papi_cpuhwcounters_t *cpu_counters,
	papi_threadhwcounters_t *thread_counters
) {
	if (backend.enabled) {
		assert(cpu_counters != NULL);
		assert(thread_counters != NULL);

		int event_set = papi_threadhwcounters_get_eventset(thread_counters);
		papi_cpuhwcounters_read_counters(cpu_counters, event_set);
	}
}
