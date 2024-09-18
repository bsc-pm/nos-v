/*
	This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2024 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <linux/limits.h>
#include <numa.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "config/config.h"
#include "defaults.h"
#include "hardware/threads.h"
#include "hardware/topology.h"
#include "hwcounters/cpuhwcounters.h"
#include "instr.h"
#include "memory/sharedmemory.h"
#include "memory/slab.h"
#include "monitoring/monitoring.h"
#include "nosv/error.h"
#include "scheduler/cpubitset.h"
#include "support/affinity.h"

#define SYS_CPU_PATH "/sys/devices/system/cpu"

thread_local int __current_cpu = -1;
__internal cpumanager_t *cpumanager;
__internal topology_t *topology;

#define snprintf_check(str, offset, maxsize, ...)                              \
	do {                                                                       \
		int ret = snprintf(str + offset, maxsize - offset, __VA_ARGS__);       \
		if (ret >= maxsize - offset) {                                         \
			nosv_abort("Failed to format locality domains. Buffer too small"); \
		}                                                                      \
		offset += ret;                                                         \
	} while (0)

static inline int topology_get_logical_id_check(nosv_topo_level_t level, int system_id)
{
	int lid = topology_get_logical_id(level, system_id);
	if (lid < 0)
		nosv_abort("system_id %d is invalid for topology level %s", system_id, nosv_topo_level_names[level]);

	return lid;
}

// Init system id to logical id mapping. Sets all values to -1, and inits topology->s_max[level]
static inline int *topology_init_domain_s_to_l(nosv_topo_level_t level, int max)
{
	topology->s_max[level] = max;
	int size = max + 1;
	int **s_to_l = &topology->s_to_l[level];
	assert(*s_to_l == NULL); // assert we have not initialized this level yet
	*s_to_l = salloc(sizeof(int) * size, 0);
	for (int i = 0; i < size; ++i) {
		(*s_to_l)[i] = -1;
	}
	return *s_to_l;
}

// Returns the array of topo_domain_t structs for the given topology level.
static inline topo_domain_t *topology_get_level_domains(nosv_topo_level_t level)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	return topology->per_level_domains[level];
}

// Returns the topo_domain_t struct for the given level and logical id.
static inline topo_domain_t *topology_get_domain(nosv_topo_level_t level, int logical_id)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	assert(logical_id >= 0);

	topo_domain_t *domains = topology_get_level_domains(level);
	return &domains[logical_id];
}

static inline void topology_domain_set_parent(topo_domain_t *domain, nosv_topo_level_t parent_lvl, int parent_logical)
{
	assert(domain->level >= NOSV_TOPO_LEVEL_NODE);
	assert(domain->level <= NOSV_TOPO_LEVEL_CPU);
	assert(parent_lvl >= NOSV_TOPO_LEVEL_NODE);
	assert(parent_lvl <= NOSV_TOPO_LEVEL_CPU);
	assert(domain->level != parent_lvl);
	// parent_lvl can be lower in the hierarcky than domain, but in that case parent_logical must be TOPO_ID_DISABLED
	assert(parent_lvl < domain->level || parent_logical == TOPO_ID_DISABLED);

	domain->parents[parent_lvl] = parent_logical;
}

static inline void topology_domain_set_ids(topo_domain_t *domain, int system_id, int logical_id)
{
	assert(domain->level >= NOSV_TOPO_LEVEL_NODE);
	assert(domain->level <= NOSV_TOPO_LEVEL_CPU);
	assert(system_id >= 0);
	assert(logical_id >= 0);
	assert(logical_id <= topology_get_level_max(domain->level));

	topology->s_to_l[domain->level][system_id] = logical_id;
	domain->system_id = system_id;
	domain->parents[domain->level] = logical_id;
}

// Sets system to logical id mapping, inits parents and logical id, and inits cpu bitsets
static inline void topology_init_domain(nosv_topo_level_t level, int system_id, int logical_id)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	cpu_bitset_set(topology_get_valid_domains_mask(level), system_id);

	topo_domain_t *dom = topology_get_domain(level, logical_id);
	dom->level = level;
	topology_domain_set_ids(dom, system_id, logical_id);

	// Init parents' array logical ids
	for (int other_lvl = NOSV_TOPO_LEVEL_NODE; other_lvl <= NOSV_TOPO_LEVEL_CPU; other_lvl++) {
		if (other_lvl == level) {
			// Same domain level, so skip
			continue;
		}

		// Set as disabled the levels lower in the hierarchy, and to unset the ones higher
		int other_lvl_logical = other_lvl > level ? TOPO_ID_DISABLED : TOPO_ID_UNSET;
		topology_domain_set_parent(dom, other_lvl, other_lvl_logical);
	}

	// Init topo_domain_t cpu_bitset_t members
	cpu_bitset_init(&(dom->cpu_sid_mask), NR_CPUS);
	cpu_bitset_init(&(dom->cpu_lid_mask), NR_CPUS);
}

// Updates cpus, cores and complex sets domains setting the value of parent domain specified with dom_level and dom_logical
static inline void topology_update_cpu_and_parents(int cpu_system, nosv_topo_level_t parent_level, int parent_logical)
{
	assert(parent_level != NOSV_TOPO_LEVEL_CPU);

	topo_domain_t *dom = topology_get_domain(parent_level, parent_logical);

	// Set cpu masks on parent
	int cpu_logical = topology_get_logical_id_check(NOSV_TOPO_LEVEL_CPU, cpu_system);
	cpu_bitset_set(&(dom->cpu_sid_mask), cpu_system);
	cpu_bitset_set(&(dom->cpu_lid_mask), cpu_logical);

	// Set parent logical id in cpu struct
	cpu_t *cpu = cpu_get_from_logical_id(cpu_logical);
	topology_domain_set_parent(cpu->cpu_domain, parent_level, parent_logical);

	// Update all levels below the parent and above the cpu with the parent logical id
	for (int cpu_p_lvl = NOSV_TOPO_LEVEL_CORE; cpu_p_lvl > parent_level; cpu_p_lvl--) {
		int cpu_p_lid = cpu_get_parent_logical_id(cpu, cpu_p_lvl);
		assert(cpu_p_lid >= 0);
		topo_domain_t *cpu_p_dom = topology_get_domain(cpu_p_lvl, cpu_p_lid);
		topology_domain_set_parent(cpu_p_dom, parent_level, parent_logical);
	}
}

// Iterate over cpus inside core and update complex set info
static inline void topology_set_complex_set_in_core_cpus(int core_logical, int cs_lid, const cpu_bitset_t *valid_cpus)
{
	cpu_bitset_t *core_cpus_sid = topology_get_cpu_system_mask(NOSV_TOPO_LEVEL_CORE, core_logical);
	int cpu_sid;
	CPU_BITSET_FOREACH (core_cpus_sid, cpu_sid) {
		if (cpu_bitset_isset(valid_cpus, cpu_sid))
			topology_update_cpu_and_parents(cpu_sid, NOSV_TOPO_LEVEL_COMPLEX_SET, cs_lid);
	}
}

static inline bool bitset_has_valid_cpus(const cpu_bitset_t *bitset, const cpu_bitset_t *valid_cpus)
{
	int cpu_system;
	CPU_BITSET_FOREACH (bitset, cpu_system) {
		if (cpu_bitset_isset(valid_cpus, cpu_system))
			return true;

	}
	return false;
}

// Inits topo_domain_t structures without the numa info
static inline void topology_init_complex_sets(cpu_bitset_t *valid_cpus, cpu_bitset_t *valid_cores)
{
	int core_cnt = cpu_bitset_count(valid_cores);
	int maxcs = core_cnt - 1;
	// Use the relevant configuration option

	topology_init_domain_s_to_l(NOSV_TOPO_LEVEL_COMPLEX_SET, maxcs);

	topo_domain_t *topo_complex_sets = (topo_domain_t *) malloc(sizeof(topo_domain_t) * core_cnt);
	topology->per_level_domains[NOSV_TOPO_LEVEL_COMPLEX_SET] = topo_complex_sets;


	cpu_bitset_t visited_cores;
	cpu_bitset_init(&visited_cores, NR_CPUS);
	generic_array_t *config_cs = (generic_array_t *) (&nosv_config.topology_complex_sets);

	int cs_logical = 0; // logical id
	int cs_system;
	for (cs_system = 0; cs_system < config_cs->n; ++cs_system) {
		char *bitset_str = ((char **) config_cs->items)[cs_system];
		cpu_bitset_t cs_cpus;
		if (cpu_bitset_parse_str(&cs_cpus, bitset_str))
			nosv_abort("Could not parse complex set %d: %s", cs_system, bitset_str);

		// Ignore complex sets that have no valid cpus
		if (!cpu_bitset_overlap(&cs_cpus, valid_cpus))
			continue;

		if (cpu_bitset_count(&cs_cpus) == 0) {
			nosv_warn("All complex sets should have at least 1 cpu in the config file. Ignoring complex set %d.", cs_system);
			continue;
		}
		topology_init_domain(NOSV_TOPO_LEVEL_COMPLEX_SET, cs_system, cs_logical);

		int cpu_system;
		CPU_BITSET_FOREACH (&cs_cpus, cpu_system) {
			int cpu_logical = topology_get_logical_id_check(NOSV_TOPO_LEVEL_CPU, cpu_system);
			int core_logical = topology_get_parent_logical_id(NOSV_TOPO_LEVEL_CPU, cpu_logical, NOSV_TOPO_LEVEL_CORE);
			int core_system = topology_get_system_id(NOSV_TOPO_LEVEL_CORE, core_logical);
			if (!cpu_bitset_isset(valid_cores, core_system))
				continue;

			if (cpu_bitset_isset(&visited_cores, core_system))
				nosv_abort("system core %d configured to be included in complex set %d, but it is already included in another complex set.", core_system, cs_system);

			cpu_bitset_set(&visited_cores, (int) core_system);

			topology_set_complex_set_in_core_cpus(core_logical, cs_logical, valid_cpus);
		}
		cs_logical++;
	}

	// Now, make a CS for each core that was not visited
	int core_sid;
	CPU_BITSET_FOREACH (valid_cores, core_sid) {
		if (!cpu_bitset_isset(&visited_cores, core_sid)) {
			topology_init_domain(NOSV_TOPO_LEVEL_COMPLEX_SET, cs_system, cs_logical);

			int core_lid = topology_get_logical_id_check(NOSV_TOPO_LEVEL_CORE, core_sid);
			topology_set_complex_set_in_core_cpus(core_lid, cs_logical, valid_cpus);

			cs_system++;
			cs_logical++;
		}
	}

	// Move malloc'd CSs to shared memory
	topo_domain_t *shmem_cs = salloc(sizeof(topo_domain_t) * cs_logical, 0);
	memcpy(shmem_cs, topo_complex_sets, sizeof(topo_domain_t) * cs_logical);
	free(topo_complex_sets);
	topology->per_level_domains[NOSV_TOPO_LEVEL_COMPLEX_SET] = shmem_cs;
	topology->per_level_count[NOSV_TOPO_LEVEL_COMPLEX_SET] = cs_logical;

	assert(cs_logical <= maxcs + 1);
}

// Returns core system id, and populates core_cpus bitset.
// We infer real system core id, which we define as the first cpu sibling of the core.
// Cannot use core_id sysfs value as it is not unique in a system
static inline int topology_get_core_valid_cpus(const int cpu_system, const cpu_bitset_t *const valid_cpus, cpu_bitset_t *core_cpus)
{
	// First, open thread_siblings file and read content
	char siblings_filename[PATH_MAX];
	int would_be_size = snprintf(siblings_filename, PATH_MAX, SYS_CPU_PATH "/cpu%d/topology/thread_siblings_list", cpu_system);
	if (would_be_size >= PATH_MAX)
		nosv_abort("snprintf failed to format siblings_filename string. String too big with size %d", would_be_size);

	FILE *siblings_fp = fopen(siblings_filename, "r");
	if (!siblings_fp)
		nosv_abort("Couldn't open cpu thread siblings list file %s", siblings_filename);

	char core_cpus_str[40];
	char *fgets_ret = fgets(core_cpus_str, 40, siblings_fp);
	if (!fgets_ret)
		nosv_abort("Couldn't read cpu thread siblings list file %s", siblings_filename);

	int fclose_ret = fclose(siblings_fp);
	// Check closed correctly
	if (fclose_ret == EOF)
		nosv_abort("Couldn't close cpu thread siblings list file %s", siblings_filename);

	if (cpu_bitset_parse_str(core_cpus, core_cpus_str))
		nosv_abort("Could not parse core cpu list: %s", core_cpus_str);

	cpu_bitset_and(core_cpus, valid_cpus);
	return cpu_bitset_ffs(core_cpus);
}

// Inits core topo_domain_t structs without the numa and complex set info
static inline void topology_init_cores(cpu_bitset_t const *const valid_cpus)
{
	// Init system to logical array
	topology_init_domain_s_to_l(NOSV_TOPO_LEVEL_CORE, NR_CPUS);

	cpu_bitset_t *valid_cores = topology_get_valid_domains_mask(NOSV_TOPO_LEVEL_CORE);
	cpu_bitset_init(valid_cores, NR_CPUS);
	cpu_bitset_t visited_cpus;
	cpu_bitset_init(&visited_cpus, NR_CPUS);

	topo_domain_t *topo_cores = malloc(sizeof(topo_domain_t) * cpu_bitset_count(valid_cpus));
	topology->per_level_domains[NOSV_TOPO_LEVEL_CORE] = topo_cores;

	int cpu_system = 0;
	int core_logical = 0;
	CPU_BITSET_FOREACH (valid_cpus, cpu_system) {
		if (!cpu_bitset_isset(&visited_cpus, cpu_system)) {
			// Find core system id and find core cpus
			cpu_bitset_t core_valid_cpus;
			int core_system = topology_get_core_valid_cpus(cpu_system, valid_cpus, &core_valid_cpus);

			// Init core topology domain
			topology_init_domain(NOSV_TOPO_LEVEL_CORE, core_system, core_logical);

			// Update cpu and parents
			int core_cpu_system;
			CPU_BITSET_FOREACH (&core_valid_cpus, core_cpu_system) {
				assert(!cpu_bitset_isset(&visited_cpus, core_cpu_system));
				cpu_bitset_set(&visited_cpus, core_cpu_system);

				topology_update_cpu_and_parents(core_cpu_system, NOSV_TOPO_LEVEL_CORE, core_logical);
			}
			core_logical++;
		}
	}
	int *topo_core_cnt = &(topology->per_level_count[NOSV_TOPO_LEVEL_CORE]);
	*topo_core_cnt = cpu_bitset_count(valid_cores);
	assert(core_logical == (*topo_core_cnt));

	topo_domain_t *shmem_cores = salloc(sizeof(topo_domain_t) * (*topo_core_cnt), 0);
	memcpy(shmem_cores, topo_cores, sizeof(topo_domain_t) * (*topo_core_cnt));
	free(topo_cores);
	topology->per_level_domains[NOSV_TOPO_LEVEL_CORE] = shmem_cores;
}

static inline void topology_init_node(cpu_bitset_t *valid_cpus)
{
	topology_init_domain_s_to_l(NOSV_TOPO_LEVEL_NODE, 0);
	topology->s_max[NOSV_TOPO_LEVEL_NODE] = 0;

	topology->per_level_domains[NOSV_TOPO_LEVEL_NODE] = (topo_domain_t *) salloc(sizeof(topo_domain_t), 0);
	topology->per_level_count[NOSV_TOPO_LEVEL_NODE] = 1;
	cpu_bitset_init(topology_get_valid_domains_mask(NOSV_TOPO_LEVEL_NODE), NR_CPUS);

	topology_init_domain(NOSV_TOPO_LEVEL_NODE, 0, 0);

	int cpu_system;
	CPU_BITSET_FOREACH (valid_cpus, cpu_system) {
		topology_update_cpu_and_parents(cpu_system, NOSV_TOPO_LEVEL_NODE, 0);
	}
}

// Inits topology->numas, updates cpus, cores and complex sets domains setting numa logical id. Also inits numa system to logical map, and valid_numas bitset
static inline void topology_init_numa_from_config(cpu_bitset_t *valid_cpus, generic_array_t *all_numa_config)
{
	topology->numa_fromcfg = 1;
	int cpus_cnt = cpu_bitset_count((valid_cpus));
	// Use the relevant configuration option
	int config_numa_count = all_numa_config->n;
	topology_init_domain_s_to_l(NOSV_TOPO_LEVEL_NUMA, config_numa_count);
	assert(config_numa_count);
	cpu_bitset_init(topology_get_valid_domains_mask(NOSV_TOPO_LEVEL_NUMA), NR_CPUS);

	if (config_numa_count > cpus_cnt)
		nosv_warn("Error parsing config file. Number of numas (%d) is greater than number of cpus (%d).", config_numa_count, cpus_cnt);


	// Temporary malloc'd array
	topo_domain_t *topo_numas = (topo_domain_t *) malloc(sizeof(topo_domain_t) * config_numa_count);
	topology->per_level_domains[NOSV_TOPO_LEVEL_NUMA] = topo_numas;

	cpu_bitset_t visited_cpus;
	cpu_bitset_init(&visited_cpus, NR_CPUS);

	int numa_logical = 0;
	for (int numa_system = 0; numa_system < config_numa_count; ++numa_system) {
		char *bitset_str = ((char **) all_numa_config->items)[numa_system];
		if (!bitset_str || !*bitset_str)
			nosv_abort("Error parsing config file. Null or empty string as numa configuration.");

		cpu_bitset_t numa_cpus;
		if (cpu_bitset_parse_str(&numa_cpus, bitset_str))
			nosv_abort("Could not parse complex set %d: %s", numa_system, bitset_str);

		if (cpu_bitset_count(&numa_cpus) == 0)
			nosv_warn("Error parsing config file. All numas must have at least 1 cpu in the config file.");

		// Ignore numas that have no valid cpus
		if (!bitset_has_valid_cpus(&numa_cpus, valid_cpus))
			continue;

		topology_init_domain(NOSV_TOPO_LEVEL_NUMA, numa_system, numa_logical);

		int cpu_system;
		CPU_BITSET_FOREACH (&numa_cpus, cpu_system) {
			// Check cpu is valid
			if (!cpu_bitset_isset(valid_cpus, cpu_system))
				continue;

			// Check cpu is not already included
			if (cpu_bitset_isset(&visited_cpus, cpu_system))
				nosv_abort("cpu %d configured to be included in numa %d, but cpu is already included in another numa.", cpu_system, numa_system);

			cpu_bitset_set(&visited_cpus, cpu_system);

			// Update topology tree
			topology_update_cpu_and_parents(cpu_system, NOSV_TOPO_LEVEL_NUMA, numa_logical);
		}

		numa_logical++;
	}

	if (cpu_bitset_cmp(&visited_cpus, topology_get_valid_domains_mask(NOSV_TOPO_LEVEL_CPU)))
		nosv_abort("Numa config has different cpu count than the valid cpus");

	// Move malloc'd CSs to shared memory
	topo_domain_t *shmem_numa = salloc(sizeof(topo_domain_t) * numa_logical, 0);
	memcpy(shmem_numa, topo_numas, sizeof(topo_domain_t) * numa_logical);
	free(topo_numas);
	topology->per_level_domains[NOSV_TOPO_LEVEL_NUMA] = shmem_numa;
	topology->per_level_count[NOSV_TOPO_LEVEL_NUMA] = numa_logical;
}

// Numa is valid if has at least 1 cpu in valid_cpus
static inline bool topology_check_numa_is_valid_libnuma(int numa_system, cpu_bitset_t *valid_cpus)
{
	struct bitmask *cpus_in_numa = numa_allocate_cpumask();
	int ret = numa_node_to_cpus(numa_system, cpus_in_numa);
	if (ret < 0)
		nosv_abort("Error: Could not get cpus for numa node %d", numa_system);

	int valid_cpu;
	bool valid_numa = false;
	CPU_BITSET_FOREACH (valid_cpus, valid_cpu) {
		if (numa_bitmask_isbitset(cpus_in_numa, valid_cpu)) {
			valid_numa = true;
			break;
		}
	}

	numa_free_cpumask(cpus_in_numa);
	return valid_numa;
}

// Inits topology->numas, updates cpus, cores and complex sets domains setting numa logical id
static inline void topology_init_numa_from_libnuma(cpu_bitset_t *valid_cpus)
{
	// Use numa_all_nodes_ptr as that contains only the nodes that are actually available,
	// not all configured. On some machines, some nodes are configured but unavailable.
	int numa_count = numa_bitmask_weight(numa_all_nodes_ptr);
	topology->per_level_count[NOSV_TOPO_LEVEL_NUMA] = numa_count;
	int numa_max = numa_max_node();
	topology_init_domain_s_to_l(NOSV_TOPO_LEVEL_NUMA, numa_max);
	if (numa_max < 0)
		nosv_abort("Error: Number of numa nodes is %d, which is invalid.", numa_max);

	cpu_bitset_init(topology_get_valid_domains_mask(NOSV_TOPO_LEVEL_NUMA), NR_CPUS);

	topology->per_level_domains[NOSV_TOPO_LEVEL_NUMA] = (topo_domain_t *) salloc(sizeof(topo_domain_t) * numa_count, 0);

	int libnuma_invalid_numas = 0;
	int logical_id = 0;
	for (int i = 0; i <= numa_max; ++i) {
		if (numa_bitmask_isbitset(numa_all_nodes_ptr, i)) {
			if (topology_check_numa_is_valid_libnuma(i, valid_cpus)) {
				topology_init_domain(NOSV_TOPO_LEVEL_NUMA, i, logical_id);
				logical_id++;
			} else {
				libnuma_invalid_numas++;
			}
		}
	}

	// Update cpus, cores and complex sets with numa logical id
	cpu_bitset_t visited_cpus;
	cpu_bitset_init(&visited_cpus, NR_CPUS);
	int cpu_sid;
	CPU_BITSET_FOREACH (valid_cpus, cpu_sid) {
		int numa_system = numa_node_of_cpu(cpu_sid); // notice this is libnuma
		if (numa_system < 0)
			nosv_abort("Internal error: Could not find NUMA system id for cpu %d", cpu_sid);

		// Check logical id is valid
		int numa_logical = topology_get_logical_id_check(NOSV_TOPO_LEVEL_NUMA, numa_system);
		if (numa_logical < 0 || numa_logical >= numa_count)
			nosv_abort("Internal error: Could not find NUMA logical id for cpu %d", cpu_sid);

		assert(!cpu_bitset_isset(&visited_cpus, cpu_sid));
		cpu_bitset_set(&visited_cpus, cpu_sid);

		topology_update_cpu_and_parents(cpu_sid, NOSV_TOPO_LEVEL_NUMA, numa_logical);
	}

	if (cpu_bitset_cmp(&visited_cpus, valid_cpus))
		nosv_abort("Not all cpus from valid cpus bitset were visited when parsing numas from libnuma");

	if ((cpu_bitset_count(topology_get_valid_domains_mask(NOSV_TOPO_LEVEL_NUMA)) + libnuma_invalid_numas) != numa_count)
		nosv_abort("Not all numas from libnuma were visited when parsing numas");
}

// Init numa if no numa config and libnuma is not available
static inline void topology_init_numa_from_none(cpu_bitset_t *valid_cpus)
{
	// Insert all cpus in the same numa
	// Create a cpu list string to pass to "init_numa_from_config"
	int cpu_count = cpu_bitset_count(valid_cpus);
	int cpulist_maxsize = cpu_count * 4;
	char *cpulist = malloc(sizeof(char) * cpulist_maxsize);
	cpulist[0] = '\0';

	// Iterate over valid cpus and add them to the only one numa cpu_list
	int cpu_system;
	int i = 0;
	int strsize = strlen(cpulist);
	CPU_BITSET_FOREACH (valid_cpus, cpu_system) {
		snprintf_check(cpulist, strsize, cpulist_maxsize, "%s%d", i > 0 ? "," : "", cpu_system);
		i++;
	}

	// Create numa config with all cpus in one numa
	generic_array_t numa_nodes;
	numa_nodes.n = i;
	numa_nodes.items = cpulist;

	// Lastly, init numa using generic array
	topology_init_numa_from_config(valid_cpus, &numa_nodes);
	free(cpulist);
}

static inline void topology_init_numa(cpu_bitset_t *valid_cpus)
{
	if (nosv_config.topology_numa_nodes.n >= 1) { // If more than 1, enable numa from config
		topology_init_numa_from_config(valid_cpus, &nosv_config.topology_numa_nodes);
	} else if (numa_available() != -1) {
		topology_init_numa_from_libnuma(valid_cpus);
	} else {
		topology_init_numa_from_none(valid_cpus);
	}
}

// Get a valid binding mask which includes all online CPUs in the system
void cpu_get_all_mask(const char **mask)
{
	// One would reasonably think that we can discover all runnable CPUs by
	// parsing /sys/devices/system/cpu/online, but the A64FX reports as online
	// CPUs 0-1,12-59, and 0-1 are not actually usable CPUs.
	// The only way to properly determine CPUs there is to use the parsed mask
	// for sched_setaffinity and then get the "corrected" affinity back with
	// sched_setaffinity.

	cpu_set_t set, bkp;
	char online_mask[400];

	FILE *fp = fopen(SYS_CPU_PATH "/online", "r");
	if (!fp)
		nosv_abort("Failed to open online CPU list");

	fgets(online_mask, sizeof(online_mask), fp);
	fclose(fp);

	// Parse the online mask and copy it to a cpu_set_t (needed for sched_setaffinity API)
	cpu_bitset_t parsed;
	cpu_bitset_init(&parsed, CPU_SETSIZE);
	cpu_bitset_parse_str(&parsed, online_mask);
	cpu_bitset_to_cpuset(&set, &parsed);

	// Now the A64FX dance
	bypass_sched_getaffinity(0, sizeof(cpu_set_t), &bkp);
	bypass_sched_setaffinity(0, sizeof(cpu_set_t), &set);
	bypass_sched_getaffinity(0, sizeof(cpu_set_t), &set);
	bypass_sched_setaffinity(0, sizeof(cpu_set_t), &bkp);

	// At this point, "set" should contain all *really* online CPUs
	// Now we have to translate it to an actual mask
	assert(CPU_COUNT(&set) > 0);
	int maxcpu = CPU_SETSIZE - 1;
	while (!CPU_ISSET(maxcpu, &set))
		--maxcpu;

	assert(maxcpu >= 0);
	int num_digits = round_up_div(maxcpu, 4);
	int str_size = num_digits + 2 /* 0x */ + 1 /* \0 */;

	char *res = malloc(str_size);
	res[str_size - 1] = '\0';
	res[0] = '0';
	res[1] = 'x';
	char *curr_digit = &res[str_size - 2];

	for (int i = 0; i < num_digits; ++i) {
		int tmp = 0;
		for (int cpu = i * 4; cpu < (i + 1) * 4 && cpu <= maxcpu; ++cpu)
			tmp |= ((!!CPU_ISSET(cpu, &set)) << (cpu % 4));

		if (tmp < 10)
			*curr_digit = '0' + tmp;
		else
			*curr_digit = 'a' - 10 + tmp;

		curr_digit--;
	}

	*mask = res;
}

static inline void cpus_get_binding_mask(const char *binding, cpu_bitset_t *cpuset)
{
	assert(binding);
	assert(cpuset);

	cpu_set_t glibc_cpuset;
	CPU_ZERO(&glibc_cpuset);

	if (strcmp(binding, "inherit") == 0) {
		bypass_sched_getaffinity(0, sizeof(cpu_set_t), &glibc_cpuset);
		assert(CPU_COUNT(&glibc_cpuset) > 0);
	} else {
		if (binding[0] != '0' || (binding[1] != 'x' && binding[1] != 'X'))
			nosv_abort("invalid binding mask");

		const int len = strlen(binding);
		for (int c = len - 1, b = 0; c >= 2; --c, b += 4) {
			int number = 0;
			if (binding[c] >= '0' && binding[c] <= '9') {
				number = binding[c] - '0';
			} else if (binding[c] >= 'a' && binding[c] <= 'f') {
				number = (binding[c] - 'a') + 10;
			} else if (binding[c] >= 'A' && binding[c] <= 'F') {
				number = (binding[c] - 'A') + 10;
			} else {
				nosv_abort("Invalid binding mask");
			}
			assert(number >= 0 && number < 16);

			if (number & 0x1)
				CPU_SET(b + 0, &glibc_cpuset);
			if (number & 0x2)
				CPU_SET(b + 1, &glibc_cpuset);
			if (number & 0x4)
				CPU_SET(b + 2, &glibc_cpuset);
			if (number & 0x8)
				CPU_SET(b + 3, &glibc_cpuset);
		}
	}

	assert(CPU_COUNT(&glibc_cpuset) <= NR_CPUS);

	cpu_bitset_init(cpuset, NR_CPUS);
	for (int i = 0; i < NR_CPUS; i++) {
		if (CPU_ISSET(i, &glibc_cpuset)) {
			cpu_bitset_set(cpuset, i);
		}
	}
	assert(CPU_COUNT(&glibc_cpuset) == cpu_bitset_count(cpuset));
}

// Inits topo_domain_t structures without the numa, core and complex set info
static inline void topology_init_cpus(cpu_bitset_t *valid_cpus)
{
	cpus_get_binding_mask(nosv_config.topology_binding, valid_cpus);

	int cnt = cpu_bitset_count(valid_cpus);
	assert(cnt > 0);

	// The CPU array is located just after the CPU manager, as a flexible array member.
	topology->per_level_domains[NOSV_TOPO_LEVEL_CPU] = salloc(sizeof(topo_domain_t) * cnt, 0);
	cpumanager = salloc(sizeof(cpumanager_t) + cnt * sizeof(cpu_t), 0);
	st_config.config->cpumanager_ptr = cpumanager;
	topology->per_level_count[NOSV_TOPO_LEVEL_CPU] = cnt;
	assert(cnt <= NR_CPUS);

	// Find out maximum CPU id
	int maxcpu = cpu_bitset_fls(valid_cpus);
	assert(maxcpu >= 0);

	// Inform the instrumentation of all available CPUsvalid_cpus
	instr_cpu_count(cnt, maxcpu);

	// Init the system to logical array to -1
	topology_init_domain_s_to_l(NOSV_TOPO_LEVEL_CPU, NR_CPUS);

	// Save bitset in topology struct
	*topology_get_valid_domains_mask(NOSV_TOPO_LEVEL_CPU) = *valid_cpus;

	// Init pids_cpus array
	cpumanager->pids_cpus = salloc(sizeof(int) * cnt, 0);

	int cpu_logical = 0;
	int cpu_system;
	CPU_BITSET_FOREACH (valid_cpus, cpu_system) {
		topology_init_domain(NOSV_TOPO_LEVEL_CPU, cpu_system, cpu_logical);
		cpumanager->cpus[cpu_logical].cpu_domain = topology_get_domain(NOSV_TOPO_LEVEL_CPU, cpu_logical);

		// Hardware counters
		cpuhwcounters_initialize(&(cpumanager->cpus[cpu_logical].counters));

		// Inform the instrumentation of a new CPU
		instr_cpu_id(cpu_logical, cpu_system);


		// Init cpu_set_t
		CPU_ZERO(&cpumanager->cpus[cpu_logical].cpuset);
		CPU_SET(cpu_system, &cpumanager->cpus[cpu_logical].cpuset);

		// Initialize the mapping as empty
		cpumanager->pids_cpus[cpu_logical] = -1;

		cpu_logical++;
	}
}

static inline void topology_print(void)
{
	int maxsize = 100 * topology_get_level_count(NOSV_TOPO_LEVEL_CORE)															   // core lines
				  + 35 * topology_get_level_count(NOSV_TOPO_LEVEL_CPU)															   // HT lines
				  + 40 * topology_get_level_count(NOSV_TOPO_LEVEL_COMPLEX_SET) + topology_get_level_count(NOSV_TOPO_LEVEL_CPU) * 4 // complex set lines
				  + topology_get_level_count(NOSV_TOPO_LEVEL_NUMA) * 40 + topology_get_level_count(NOSV_TOPO_LEVEL_CPU) * 4 + 200; // NUMA lines
	char *msg = malloc(maxsize * sizeof(char));

	int offset = snprintf(msg, maxsize, "NOSV: Printing locality domains");
	if (offset >= maxsize)
		nosv_abort("Failed to format locality domains");

	snprintf_check(msg, offset, maxsize, "\nNOSV: NODE: 1");

	snprintf_check(msg, offset, maxsize, "\nNOSV: NUMA: system cpus contained in each numa node");
	for (int lid = 0; lid < topology_get_level_count(NOSV_TOPO_LEVEL_NUMA); lid++) {
		topo_domain_t *numa = topology_get_domain(NOSV_TOPO_LEVEL_NUMA, lid);
		int size = cpu_bitset_count(&numa->cpu_sid_mask);
		snprintf_check(msg, offset, maxsize, "\nNOSV: \t");
		snprintf_check(msg, offset, maxsize, "numa(logic=%d, system=%d, num_items=%d) = [", lid, numa->system_id, size);
		int count = 0;
		int cpu;
		CPU_BITSET_FOREACH (&numa->cpu_sid_mask, cpu) {
			snprintf_check(msg, offset, maxsize, "%s%d", count++ > 0 ? "," : "", cpu);
		}
		snprintf_check(msg, offset, maxsize, "] ");
	}

	snprintf_check(msg, offset, maxsize, "\nNOSV: COMPLEX_SETS: system cpus contained in each core complex");
	for (int lid = 0; lid < topology_get_level_count(NOSV_TOPO_LEVEL_COMPLEX_SET); lid++) {
		topo_domain_t *ccx = topology_get_domain(NOSV_TOPO_LEVEL_COMPLEX_SET, lid);
		int size = cpu_bitset_count(&ccx->cpu_sid_mask);
		snprintf_check(msg, offset, maxsize, "\nNOSV: \t");
		snprintf_check(msg, offset, maxsize, "CS(logic=%d, system=N/A, num_items=%d) = [", lid, size);
		int count = 0;
		int cpu;
		CPU_BITSET_FOREACH (&ccx->cpu_sid_mask, cpu) {
			snprintf_check(msg, offset, maxsize, "%s%d", count++ > 0 ? "," : "", cpu);
		}
		snprintf_check(msg, offset, maxsize, "] ");
	}

	snprintf_check(msg, offset, maxsize, "\nNOSV: CORE: system cpus contained in each core");
	for (int lid = 0; lid < topology_get_level_count(NOSV_TOPO_LEVEL_CORE); lid++) {
		topo_domain_t *core = topology_get_domain(NOSV_TOPO_LEVEL_CORE, lid);
		int size = cpu_bitset_count(&core->cpu_sid_mask);
		snprintf_check(msg, offset, maxsize, "\nNOSV: \t");
		snprintf_check(msg, offset, maxsize, "core(logic=%d, system=%d, num_items=%d) = [", lid, core->system_id, size);
		int count = 0;
		int cpu;
		CPU_BITSET_FOREACH (&core->cpu_sid_mask, cpu) {
			snprintf_check(msg, offset, maxsize, "%s%d", count++ > 0 ? "," : "", cpu);
		}
		snprintf_check(msg, offset, maxsize, "] ");
	}

	snprintf_check(msg, offset, maxsize, "\nNOSV: CPU: cpu(logic=lid, system=sid)");
	for (int lid = 0; lid < topology_get_level_count(NOSV_TOPO_LEVEL_CPU); lid++) {
		cpu_t *cpu = &cpumanager->cpus[lid];
		snprintf_check(msg, offset, maxsize, "\nNOSV: \t");
		snprintf_check(msg, offset, maxsize, "cpu(logic=%d, system=%d)", cpu_get_logical_id(cpu), cpu_get_system_id(cpu));
	}

	nosv_warn("%s", msg);
	free(msg);
}

// Asserts parent is set in domain structure
static inline void topology_assert_parent_is_set(nosv_topo_level_t child, nosv_topo_level_t parent)
{
	assert(child >= NOSV_TOPO_LEVEL_NUMA && child <= NOSV_TOPO_LEVEL_CPU);
	assert(parent >= NOSV_TOPO_LEVEL_NODE && parent <= NOSV_TOPO_LEVEL_CORE);

	topo_domain_t *arr = topology_get_level_domains(child);
	for (int i = 0; i < topology->per_level_count[child]; ++i) {
		if (arr[i].parents[parent] < 0) {
			nosv_abort("parent %s not set for %s with idx %d. Check initialization of %s",
				topology_get_level_name(parent), topology_get_level_name(child), arr[i].system_id, topology_get_level_name(parent));
		}
	}
}

// Asserts that for every sibling domain, the parents are the same
static inline void topology_assert_siblings_have_same_parent(void)
{
	for (int level = NOSV_TOPO_LEVEL_CORE; level >= NOSV_TOPO_LEVEL_NUMA; level--) {
		topo_domain_t *dom_arr = topology_get_level_domains(level);
		for (int parent = level - 1; parent >= NOSV_TOPO_LEVEL_NUMA; parent--) {
			cpu_bitset_t cpus_visited;
			cpu_bitset_init(&cpus_visited, NR_CPUS);

			for (int i = 0; i < topology->per_level_count[level]; ++i) {
				topo_domain_t *domain = &dom_arr[i];
				int cpu_lid;
				int last_parent_lid = TOPO_ID_UNSET;
				int last_cpu_sid = TOPO_ID_UNSET;
				CPU_BITSET_FOREACH (&domain->cpu_lid_mask, cpu_lid) {
					cpu_t *cpu = cpu_get_from_logical_id(cpu_lid);

					int parent_lid = cpu_get_parent_logical_id(cpu, parent);
					assert(parent_lid >= 0);

					// Assert all cpus in this domain have the same parent
					if (last_parent_lid == TOPO_ID_UNSET) {
						last_parent_lid = parent_lid;
						last_cpu_sid = cpu_get_system_id(cpu);
					}

					if (last_parent_lid != parent_lid) {
						nosv_abort("CPU siblings in domain level %s with system ids (%d, %d) have different parent %s (logical:%d != logical:%d). Check config finitializationile for %s",
							topology_get_level_name(level), cpu_get_system_id(cpu), last_cpu_sid, topology_get_level_name(parent), parent_lid, last_parent_lid, topology_get_level_name(parent));
					}
				}
			}
		}
	}
}

static inline void topology_assert_parents_set(void)
{
	for (int child = NOSV_TOPO_LEVEL_NUMA; child <= NOSV_TOPO_LEVEL_CPU; child++) {
		for (int parent = NOSV_TOPO_LEVEL_NODE; parent < child; parent++) {
			topology_assert_parent_is_set(child, parent);
		}
	}
}

void topology_init(int initialize)
{
	if (!initialize) {
		cpumanager = (cpumanager_t *) st_config.config->cpumanager_ptr;
		assert(cpumanager);
		topology = (topology_t *) st_config.config->topology_ptr;
		assert(topology);
		return;
	}

	topology = salloc(sizeof(topology_t), 0);
	st_config.config->topology_ptr = topology;

	// All levels unitialized
	for (int d = 0; d < NOSV_TOPO_LEVEL_COUNT; ++d) {
		topology->s_to_l[d] = NULL;
		topology->s_max[d] = TOPO_ID_UNSET;
	}

	// Initialize domain levels
	cpu_bitset_t valid_cpus;
	topology_init_cpus(&valid_cpus);
	topology_init_cores(&valid_cpus);
	topology_init_complex_sets(&valid_cpus, topology_get_valid_domains_mask(NOSV_TOPO_LEVEL_CORE));
	topology_init_numa(&valid_cpus);
	topology_init_node(&valid_cpus);

	topology_assert_parents_set();
	topology_assert_siblings_have_same_parent();

	const char *env_print_locality_domains = getenv("NOSV_PRINT_TOPOLOGY");
	if (env_print_locality_domains != NULL && (strcmp(env_print_locality_domains, "TRUE") == 0 || strcmp(env_print_locality_domains, "true") == 0))
		topology_print();
}

void topology_free(void)
{
	// Free cpumanager
	int cpu_cnt = topology_get_level_count(NOSV_TOPO_LEVEL_CPU);
	sfree(cpumanager->pids_cpus, cpu_cnt, 0);
	sfree(cpumanager, sizeof(cpumanager_t) + cpu_cnt * sizeof(cpu_t), 0);
	cpumanager = NULL;

	// Free topology system to logical id array, and per level domain array
	for (int lvl = 0; lvl < NOSV_TOPO_LEVEL_COUNT; ++lvl) {
		// Per level, free first system to logical map array
		if (topology->s_to_l[lvl]) {
			sfree(topology->s_to_l[lvl], sizeof(int) * (topology_get_level_max(lvl) + 1), 0);
			topology->s_to_l[lvl] = NULL;
		}
		// Per level, free domain array
		if (topology->per_level_domains[lvl]) {
			sfree(topology->per_level_domains[lvl], sizeof(topo_domain_t) * topology_get_level_count(lvl), 0);
			topology->per_level_domains[lvl] = NULL;
		}
	}

	// Free topology main pointer
	sfree(topology, sizeof(topology_t), 0);
	topology = NULL;
}

int topology_get_default_affinity(char **out)
{
	struct bitmask *all_affinity = numa_allocate_cpumask();
	int max_cpus = numa_num_possible_cpus();
	assert(all_affinity);

	numa_sched_getaffinity(0, all_affinity);

	if (numa_bitmask_weight(all_affinity) == 1) {
		// Affinity to a single core
		int i;
		for (i = 0; i < max_cpus - 1; ++i) {
			if (numa_bitmask_isbitset(all_affinity, i))
				break;
		}

		assert(numa_bitmask_isbitset(all_affinity, i));
		__maybe_unused int res = nosv_asprintf(out, "cpu-%d", i);
		assert(!res);
	} else {
		int selected_node = -1;
		for (int i = 0; i < max_cpus - 1; ++i) {
			if (numa_bitmask_isbitset(all_affinity, i)) {
				int node_of_cpu = numa_node_of_cpu(i);
				if (selected_node < 0)
					selected_node = node_of_cpu;

				if (selected_node != node_of_cpu) {
					// Cannot determine single node affinity
					numa_free_cpumask(all_affinity);
					return 1;
				}
			}
		}

		assert(selected_node >= 0);
		__maybe_unused int res = nosv_asprintf(out, "numa-%d", selected_node);
		assert(!res);

		// So far, we know all CPUs belong to a single node. Nevertheless, it is possible
		// that the node has more CPUs that we don't have an affinity to.
		// Detect this case and warn about it
		struct bitmask *node_affinity = numa_allocate_cpumask();
		numa_node_to_cpus(selected_node, node_affinity);

		if (!numa_bitmask_equal(all_affinity, node_affinity))
			nosv_warn("Affinity automatically set to numa-%d, but other non-affine CPUs are present in this node.", selected_node);

		numa_bitmask_free(node_affinity);
	}

	numa_free_cpumask(all_affinity);
	return 0;
}

// Returns max system id for the given topology level
int topology_get_level_max(nosv_topo_level_t level)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	return topology->s_max[level];
}

// Returns the logical id given the topology level and system id, -1 if not yet initialized
int topology_get_logical_id(nosv_topo_level_t level, int system_id)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	if (topology->s_max[level] < 0)
		return TOPO_ID_UNSET; // Level not yet initialized

	if (system_id > topology->s_max[level])
		nosv_abort("system_id %d is larger than the maximum system_id %d for topology level %s", system_id, topology->s_max[level], nosv_topo_level_names[level]);

	assert(topology->s_to_l[level][system_id] >= -1);
	return topology->s_to_l[level][system_id];
}

// Returns the system id given the topology level and logical id
int topology_get_system_id(nosv_topo_level_t level, int logical_id)
{
	assert(logical_id >= 0 && topology->per_level_count[level]);
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	if (topology->s_max[level] < 0)
		return TOPO_ID_UNSET; // Level not yet initialized

	topo_domain_t *domains = topology_get_level_domains(level);
	int system_id = domains[logical_id].system_id;
	assert(system_id >= 0 && system_id <= topology->s_max[level]);
	return system_id;
}

// Returns the number of domains in the given topology level
int topology_get_level_count(nosv_topo_level_t level)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	assert(topology->per_level_count[level] >= 1);

	return topology->per_level_count[level];
}

// Returns the name (char array) of the given topology level
const char *topology_get_level_name(nosv_topo_level_t level)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	return nosv_topo_level_names[level];
}

// Returns the logical id of the parent
int topology_get_parent_logical_id(nosv_topo_level_t child_level, int child_logical_id, nosv_topo_level_t parent)
{
	// Node does not have parents
	assert(child_level >= NOSV_TOPO_LEVEL_NUMA && child_level <= NOSV_TOPO_LEVEL_CPU);
	assert(parent >= NOSV_TOPO_LEVEL_NODE && parent <= NOSV_TOPO_LEVEL_CPU);
	assert(child_logical_id >= 0 && child_logical_id < topology_get_level_count(child_level));
	assert(child_level >= parent);

	return topology->per_level_domains[child_level][child_logical_id].parents[parent];
}

// Returns the system id of the parent
int topology_get_parent_system_id(nosv_topo_level_t child_level, int child_logical_id, nosv_topo_level_t parent)
{
	// Node does not have parents
	assert(child_level >= NOSV_TOPO_LEVEL_NUMA && child_level <= NOSV_TOPO_LEVEL_CPU);
	assert(parent >= NOSV_TOPO_LEVEL_NODE && parent <= NOSV_TOPO_LEVEL_CORE);
	assert(child_logical_id >= 0 && child_logical_id < topology_get_level_max(child_level));
	assert(child_level > parent);

	int logical_id = topology->per_level_domains[child_level][child_logical_id].parents[parent];

	return topology_get_system_id(child_level, logical_id);
}

// Returns the cpu_bitset_t of system cpus for the domain (given by level and logical id)
cpu_bitset_t *topology_get_cpu_system_mask(nosv_topo_level_t level, int lid)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	topo_domain_t *domain = topology_get_domain(level, lid);
	return &(domain->cpu_sid_mask);
}

// Returns the cpu_bitset_t of logical cpus for the domain (given by level and logical id)
cpu_bitset_t *topology_get_cpu_logical_mask(nosv_topo_level_t level, int lid)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
	topo_domain_t *domain = topology_get_domain(level, lid);
	return &(domain->cpu_lid_mask);
}

// Returns a pointer to the cpu_bitset_t of valid domains for the given topology level
cpu_bitset_t *topology_get_valid_domains_mask(nosv_topo_level_t level)
{
	assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);

	return &(topology->per_level_valid_domains[level]);
}

// Returns the logical id of the parent in the topology level 'parent'
int cpu_get_parent_logical_id(cpu_t *cpu, nosv_topo_level_t parent)
{
	assert(parent >= NOSV_TOPO_LEVEL_NODE && parent <= NOSV_TOPO_LEVEL_CPU);
	return topology_get_parent_logical_id(NOSV_TOPO_LEVEL_CPU, cpu_get_logical_id(cpu), parent);
}

cpu_t *cpu_get_from_logical_id(int cpu_logical_id)
{
	return &cpumanager->cpus[cpu_logical_id];
}

cpu_t *cpu_get_from_system_id(int cpu_system_id)
{
	int cpu_logical_id = topology_get_logical_id(NOSV_TOPO_LEVEL_CPU, cpu_system_id);
	if (cpu_logical_id < 0)
		return NULL;

	return &cpumanager->cpus[cpu_logical_id];
}

cpu_t *cpu_pop_free(int pid)
{
	for (int i = 0; i < topology_get_level_count(NOSV_TOPO_LEVEL_CPU); ++i) {
		if (cpumanager->pids_cpus[i] == -1) {
			cpumanager->pids_cpus[i] = pid;

			// A CPU is going to become active
			monitoring_cpu_active(i);

			return cpu_get_from_logical_id(i);
		}
	}

	return NULL;
}

int cpu_get_logical_id(cpu_t *cpu)
{
	return cpu->cpu_domain->logical_cpu;
}

int cpu_get_system_id(cpu_t *cpu)
{
	return cpu->cpu_domain->system_id;
}

void cpu_set_pid(cpu_t *cpu, int pid)
{
	assert(cpumanager->pids_cpus[cpu_get_logical_id(cpu)] < MAX_PIDS);
	cpumanager->pids_cpus[cpu_get_logical_id(cpu)] = pid;
}

void cpu_mark_free(cpu_t *cpu)
{
	cpumanager->pids_cpus[cpu_get_logical_id(cpu)] = -1;

	// A CPU just went idle
	monitoring_cpu_idle(cpu_get_logical_id(cpu));
}

void cpu_transfer(int destination_pid, cpu_t *cpu, task_execution_handle_t handle)
{
	assert(cpu);
	cpumanager->pids_cpus[cpu_get_logical_id(cpu)] = destination_pid;

	// Wake up a worker from another PID to take over
	worker_wake_idle(destination_pid, cpu, handle);
}

void cpu_affinity_reset(void)
{
	instr_affinity_set(-1);
	cpu_set_t glibc_all_set;
	int cpu;
	CPU_ZERO(&glibc_all_set);
	CPU_BITSET_FOREACH (topology_get_valid_domains_mask(NOSV_TOPO_LEVEL_CPU), cpu) {
		CPU_SET(cpu, &glibc_all_set);
	}
	bypass_sched_setaffinity(0, sizeof(glibc_all_set), &glibc_all_set);
}

int nosv_get_num_cpus(void)
{
	return topology_get_level_count(NOSV_TOPO_LEVEL_CPU);
}

int nosv_get_current_logical_cpu(void)
{
	if (!worker_is_in_task())
		return NOSV_ERR_OUTSIDE_TASK;

	return cpu_get_current();
}

int nosv_get_current_system_cpu(void)
{
	if (!worker_is_in_task())
		return NOSV_ERR_OUTSIDE_TASK;

	cpu_t *cpu = cpu_get_from_logical_id(cpu_get_current());
	assert(cpu);

	return cpu_get_system_id(cpu);
}

int nosv_get_num_numa_nodes(void)
{
	return topology_get_level_count(NOSV_TOPO_LEVEL_NUMA);
}

int nosv_get_system_numa_id(int logical_numa_id)
{
	if (logical_numa_id >= topology_get_level_count(NOSV_TOPO_LEVEL_NUMA))
		return NOSV_ERR_INVALID_PARAMETER;

	return topology_get_system_id(NOSV_TOPO_LEVEL_NUMA, logical_numa_id);
}

int nosv_get_logical_numa_id(int system_numa_id)
{
	return topology_get_logical_id(NOSV_TOPO_LEVEL_NUMA, system_numa_id);
}

int nosv_get_num_cpus_in_numa(int system_numa_id)
{
	int logical_node = topology_get_logical_id(NOSV_TOPO_LEVEL_NUMA, system_numa_id);
	if (logical_node < 0)
		return NOSV_ERR_INVALID_PARAMETER;

	topo_domain_t *numa = topology_get_domain(NOSV_TOPO_LEVEL_NUMA, logical_node);
	return cpu_bitset_count(&(numa->cpu_sid_mask));
}

#undef snprintf_check
