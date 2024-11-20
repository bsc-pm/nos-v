/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2022 Barcelona Supercomputing Center (BSC)
*/

#ifndef NOSV_HWINFO_H
#define NOSV_HWINFO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nosv_topo_level {
	NOSV_TOPO_LEVEL_NODE          = 0,
	NOSV_TOPO_LEVEL_NUMA          = 1,
	NOSV_TOPO_LEVEL_COMPLEX_SET   = 2,
	NOSV_TOPO_LEVEL_CORE          = 3,
	NOSV_TOPO_LEVEL_CPU           = 4,
	NOSV_TOPO_LEVEL_COUNT         = 5
} nosv_topo_level_t;

/*
	Hardware information routines for nOS-V
	Meant for nOS-V users to ask nOS-V for hardware topology information.
	Note that this corresponds to the current nOS-V instance, not the whole
	node, and reflects the "nOS-V view" (aka with configuration overrides applied).
	These functions have non-exhaustive error checking, so care should be taken
	to not call them with wrong arguments.
*/

/* Topology domain agnostic API */
/* Get the number of available domains in the specified level in the nOS-V instance */
int nosv_get_num_domains(nosv_topo_level_t level);

/* Get malloc'd array of visible domains in the specified level by the nOS-V runtime. User must free it */
int *nosv_get_available_domains(nosv_topo_level_t level);

/* Get logical id of the domain in the specified level where the current task is running */
/* The range of logical identifiers is [0, number of domains in this level) */
/* Restriction: Can only be called from a task context */
int nosv_get_current_logical_domain(nosv_topo_level_t);

/* Get system id of the domain in the specified level where the current task is running */
/* Restriction: Can only be called from a task context */
int nosv_get_current_system_domain(nosv_topo_level_t);

/* Get the number of CPUs in the specified domain in the nOS-V instance */
int nosv_get_num_cpus_in_domain(nosv_topo_level_t level, int sid);

/* Get malloc'd an array with the CPUs of the specified domain in the nOS-V instance. User must free it */
int *nosv_get_available_cpus_in_domain(nosv_topo_level_t level, int sid);

/* CPU Information API */
/* Get number of CPUs visible by the nOS-V runtime */
int nosv_get_num_cpus(void);

/* Get malloc'd array of CPUs visible by the nOS-V runtime. User must free it */
int *nosv_get_available_cpus(void);

/* Get the logical identifier of the CPU where the current task is running */
/* The range of logical identifiers is [0, number of cpus) */
/* Restriction: Can only be called from a task context */
int nosv_get_current_logical_cpu(void);

/* Get the system identifier of the CPU where the current task is running */
/* Restriction: Can only be called from a task context */
int nosv_get_current_system_cpu(void);

/* Get the number of NUMA nodes seen by nOS-V (containing cpus allowed in this process)*/
int nosv_get_num_numa_nodes(void);

/* Get malloc'd array of NUMAs leveraged by the nOS-V runtime. User must free it */
int *nosv_get_available_numa_nodes(void);

/* Get the logical identifier of the NUMA where the current task is running */
/* The range of logical identifiers is [0, number of numa nodes) */
/* Restriction: Can only be called from a task context */
int nosv_get_current_logical_numa_node(void);

/* Get the system identifier of the NUMA node where the current task is running */
/* Restriction: Can only be called from a task context */
int nosv_get_current_system_numa_node(void);

/* Get the system identifier of the NUMA node given the logical identifier */
int nosv_get_system_numa_id(int logical_numa_id);

/* Get the logical identifier of the NUMA node given the system identifier */
int nosv_get_logical_numa_id(int system_numa_id);

/* Get the number of CPUs in the NUMA node given the system identifier */
int nosv_get_num_cpus_in_numa(int system_numa_id);

#ifdef __cplusplus
}
#endif

#endif // NOSV_HWINFO_H
