/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_HWINFO_H
#define NOSV_HWINFO_H

#ifdef __cplusplus
extern "C" {
#endif

/*
   Hardware information routines for nOS-V
   Meant for nOS-V users to ask nOS-V for NUMA node and CPU information.
   Note that this corresponds to the current nOS-V instance, not the whole
   node, and reflects the "nOS-V view" (aka with configuration overrides applied).
   These functions have non-exhaustive error checking, so care should be taken
   to not call them with wrong arguments. Moreover, some of these functions
   may be inefficiently implemented, as they aren't meant to be called often.
*/

/* Gets the number of available NUMA nodes in the nOS-V instance */
int nosv_get_num_numa_nodes(void);

/* Get the system ID of a specific NUMA node. Logical NUMA nodes in
   nOS-V go from [0, num_numa_nodes). */
int nosv_get_system_numa_id(int logical_numa_id);

/* Inverse function of nosv_get_system_numa_id */
int nosv_get_logical_numa_id(int system_numa_id);

/* Return the number of CPUs available on a specific NUMA node */
int nosv_get_num_cpus_in_numa(int system_numa_id);

#ifdef __cplusplus
}
#endif

#endif // NOSV_HWINFO_H
