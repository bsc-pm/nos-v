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
#include "nosv.h"
#include "nosv/error.h"
#include "nosv/hwinfo.h"
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
	int lid = topo_dom_lid(level, system_id);
	if (lid < 0)
		nosv_abort("system_id %d is invalid for topology level %s", system_id, nosv_topo_level_names[level]);

	return lid;
}

// Init system id to logical id mapping. Sets all values to -1, and inits topology->s_max[level]
static inline void topology_init_domain_s_to_l(nosv_topo_level_t level, int max)
{
	topology->s_max[level] = max;
	int size = max + 1;

	assert(topology->s_to_l[level] == NULL); // assert we have not initialized this level yet
	topology->s_to_l[level] = salloc(sizeof(int) * size, 0);
	for (int i = 0; i < size; ++i)
		topology->s_to_l[level][i] = -1;
}

static inline void topology_domain_set_parent(topo_domain_t *domain, nosv_topo_level_t parent_lvl, int parent_logical)
{
	assert(domain->level >= TOPO_NODE);
	assert(domain->level <= TOPO_CPU);
	assert(parent_lvl >= TOPO_NODE);
	assert(parent_lvl <= TOPO_CPU);
	assert(domain->level != parent_lvl);
	// parent_lvl can be lower in the hierachy than domain, but in that case parent_logical must be TOPO_ID_DISABLED
	assert(parent_lvl < domain->level || parent_logical == TOPO_ID_DISABLED);

	domain->parents[parent_lvl] = parent_logical;
}

static inline void topology_domain_set_ids(topo_domain_t *domain, int system_id, int logical_id)
{
	assert(domain->level >= TOPO_NODE);
	assert(domain->level <= TOPO_CPU);
	assert(system_id >= 0);
	assert(logical_id >= 0);
	assert(logical_id <= topo_lvl_max(domain->level));

	topology->s_to_l[domain->level][system_id] = logical_id;
	domain->system_id = system_id;
	domain->parents[domain->level] = logical_id;
}

// Sets system to logical id mapping, inits parents and logical id, and inits cpu bitsets
static inline void topology_init_domain(nosv_topo_level_t level, int system_id, int logical_id)
{
	assert(level >= TOPO_NODE && level <= TOPO_CPU);

	cpu_bitset_set(topo_lvl_sid_bitset(level), system_id);

	topo_domain_t *dom = topo_dom_ptr(level, logical_id);
	dom->level = level;
	topology_domain_set_ids(dom, system_id, logical_id);

	// Initialize parent logical IDs
	for (int parent_lvl = TOPO_NODE; parent_lvl < level; ++parent_lvl)
		topology_domain_set_parent(dom, parent_lvl, TOPO_ID_UNSET);

	// Children parent ids are UNSET
	for (int children_lvl = level + 1; children_lvl <= TOPO_CPU; children_lvl++)
		topology_domain_set_parent(dom, children_lvl, TOPO_ID_DISABLED);

	// Init topo_domain_t cpu_bitset_t members
	cpu_bitset_init(&(dom->cpu_sid_mask), NR_CPUS);
	cpu_bitset_init(&(dom->cpu_lid_mask), NR_CPUS);

	if (level == NOSV_TOPO_LEVEL_CPU) {
		cpu_bitset_set(&(dom->cpu_sid_mask), system_id);
		cpu_bitset_set(&(dom->cpu_lid_mask), logical_id);
	}
}

// Updates cpus, cores and complex sets domains setting the value of parent domain specified with dom_level and dom_logical
static inline void topology_update_cpu_and_parents(int cpu_system, nosv_topo_level_t parent_level, int parent_logical)
{
	assert(parent_level != TOPO_CPU);

	topo_domain_t *dom = topo_dom_ptr(parent_level, parent_logical);

	// Set cpu masks on parent
	int cpu_logical = topology_get_logical_id_check(TOPO_CPU, cpu_system);
	cpu_bitset_set(&(dom->cpu_sid_mask), cpu_system);
	cpu_bitset_set(&(dom->cpu_lid_mask), cpu_logical);

	// Set parent logical id in cpu struct
	cpu_t *cpu = cpu_ptr(cpu_logical);

	// Update all levels below the parent and above the cpu with the parent logical id
	for (int curr_level = TOPO_CPU; curr_level > parent_level; curr_level--) {
		int curr_lid = cpu_parent_lid(cpu, curr_level);
		assert(curr_lid >= 0);
		topo_domain_t *dom = topo_dom_ptr(curr_level, curr_lid);
		int prev_logical_id = topo_dom_parent_lid(curr_level, curr_lid, parent_level);

		if (prev_logical_id != TOPO_ID_UNSET && prev_logical_id != parent_logical)
			nosv_abort("While setting topology hierarchy, found a parent mismatch for level %d and logical id %d\n", curr_level, curr_lid);

		topology_domain_set_parent(dom, parent_level, parent_logical);
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

static inline topo_domain_t *topology_init_level(nosv_topo_level_t level, int max, int cnt)
{
	topology_init_domain_s_to_l(level, max);

	topo_domain_t *domain = (topo_domain_t *) malloc(sizeof(topo_domain_t) * cnt);
	topology->per_level_domains[level] = domain;
	topology->per_level_count[level] = cnt;

	return domain;
}

static inline void topology_truncate_level(nosv_topo_level_t level, int finalcnt)
{
	topo_domain_t *domain = (topo_domain_t *) salloc(sizeof(topo_domain_t) * finalcnt, -1);
	memcpy(domain, topology->per_level_domains[level], sizeof(topo_domain_t) * finalcnt);
	free(topology->per_level_domains[level]);
	topology->per_level_domains[level] = domain;
	topology->per_level_count[level] = finalcnt;
}

static inline void topology_init_from_config(nosv_topo_level_t level, cpu_bitset_t *valid_cpus, generic_array_t *config, int create_remaining)
{
	// We can't have more than the count of the level below.
	int prev_level_count = topo_lvl_cnt(level + 1);
	int max = prev_level_count - 1;

	topology_init_level(level, max, prev_level_count);

	int logical_id = 0;
	int system_id;
	int visited_cpus = 0;

	for (system_id = 0; system_id < config->n; ++system_id) {
		char *bitset_str = ((char **) config->items)[system_id];
		cpu_bitset_t cpus;
		if (cpu_bitset_parse_str(&cpus, bitset_str))
			nosv_abort("Could not parse from config %d: %s", system_id, bitset_str);

		// Ignore when there are no valid cpus
		if (!cpu_bitset_overlap(&cpus, valid_cpus))
			continue;

		if (cpu_bitset_count(&cpus) == 0) {
			nosv_warn("All complex sets should have at least 1 cpu in the config file. Ignoring complex set %d.", system_id);
			continue;
		}

		topology_init_domain(level, system_id, logical_id);

		int cpu_system;
		CPU_BITSET_FOREACH (&cpus, cpu_system) {
			// Here, we'd have to make sure that for example in the case of complex sets we don't add cpus belonging to the same
			// core to different complex sets. This is ensured by the check in the set parent hierarchy function. If it detects we're
			// _changing_ an already valid parent, we know we have a problem

			topology_update_cpu_and_parents(cpu_system, level, logical_id);
			visited_cpus++;
		}

		logical_id++;
	}

	if (create_remaining) {
		// Create one of the current elements for each previous level item not contained in any of them
		int n = topo_lvl_cnt(level + 1);

		for (int i = 0; i < n; ++i) {
			int parent_id = topo_dom_parent_lid(level + 1, i, level);
			if (parent_id == TOPO_ID_UNSET) {
				topology_init_domain(level, system_id, logical_id);

				cpu_bitset_t *domain_mask = topo_dom_cpu_sid_bitset(level + 1, i);
				int cpu_system;
				CPU_BITSET_FOREACH (domain_mask, cpu_system) {
					topology_update_cpu_and_parents(cpu_system, level, logical_id);
					visited_cpus++;
				}

				++system_id;
				++logical_id;
			}
		}
	}

	if (visited_cpus != topo_lvl_cnt(TOPO_CPU))
		nosv_abort("Did not define all CPUs in config for level %d\n", level);

	topology_truncate_level(level, logical_id);
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
	cpu_bitset_t *valid_cores = topo_lvl_sid_bitset(TOPO_CORE);
	cpu_bitset_init(valid_cores, NR_CPUS);
	cpu_bitset_t visited_cpus;
	cpu_bitset_init(&visited_cpus, NR_CPUS);

	topo_domain_t *topo_cores = topology_init_level(TOPO_CORE, NR_CPUS, NR_CPUS);
	topology->per_level_domains[TOPO_CORE] = topo_cores;

	int cpu_system = 0;
	int core_logical = 0;

	CPU_BITSET_FOREACH (valid_cpus, cpu_system) {
		if (!cpu_bitset_isset(&visited_cpus, cpu_system)) {
			// Find core system id and find core cpus
			cpu_bitset_t core_valid_cpus;
			int core_system = topology_get_core_valid_cpus(cpu_system, valid_cpus, &core_valid_cpus);

			// Init core topology domain
			topology_init_domain(TOPO_CORE, core_system, core_logical);

			// Update cpu and parents
			int core_cpu_system;
			CPU_BITSET_FOREACH (&core_valid_cpus, core_cpu_system) {
				topology_update_cpu_and_parents(core_cpu_system, TOPO_CORE, core_logical);
				cpu_bitset_set(&visited_cpus, core_cpu_system);
			}
			core_logical++;
		}
	}

	int *topo_core_cnt = &(topology->per_level_count[TOPO_CORE]);
	*topo_core_cnt = cpu_bitset_count(valid_cores);
	assert(core_logical == (*topo_core_cnt));

	topology_truncate_level(TOPO_CORE, cpu_bitset_count(valid_cores));
}

static inline void topology_init_node(cpu_bitset_t *valid_cpus)
{
	topology_init_level(TOPO_NODE, 0, 1);
	topology_truncate_level(TOPO_NODE, 1);
	cpu_bitset_init(topo_lvl_sid_bitset(TOPO_NODE), NR_CPUS);
	topology_init_domain(TOPO_NODE, 0, 0);

	int cpu_system;
	CPU_BITSET_FOREACH (valid_cpus, cpu_system) {
		topology_update_cpu_and_parents(cpu_system, TOPO_NODE, 0);
	}
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
	int libnuma_count = numa_bitmask_weight(numa_all_nodes_ptr);
	int numa_max = numa_max_node();
	topology_init_domain_s_to_l(TOPO_NUMA, numa_max);
	if (numa_max < 0)
		nosv_abort("Error: Number of numa nodes is %d, which is invalid.", numa_max);

	cpu_bitset_init(topo_lvl_sid_bitset(TOPO_NUMA), NR_CPUS);

	topology->per_level_domains[TOPO_NUMA] = (topo_domain_t *) salloc(sizeof(topo_domain_t) * libnuma_count, 0);

	int libnuma_invalid_numas = 0;
	int logical_id = 0;
	for (int i = 0; i <= numa_max; ++i) {
		if (numa_bitmask_isbitset(numa_all_nodes_ptr, i)) {
			if (topology_check_numa_is_valid_libnuma(i, valid_cpus)) {
				topology_init_domain(TOPO_NUMA, i, logical_id);
				logical_id++;
			} else {
				libnuma_invalid_numas++;
			}
		}
	}
	int numa_count = logical_id;

	// Allocate just enough memory after knowing all valid numas, move initialized domains and free old memory
	topo_domain_t *tmp = (topo_domain_t *) salloc(sizeof(topo_domain_t) * logical_id, 0);
	memcpy(tmp, topology->per_level_domains[TOPO_NUMA], sizeof(topo_domain_t) * logical_id);
	sfree(topology->per_level_domains[TOPO_NUMA], sizeof(topo_domain_t) * libnuma_count, 0);
	topology->per_level_domains[TOPO_NUMA] = tmp;

	topology->per_level_count[TOPO_NUMA] = numa_count;

	// Update cpus, cores and complex sets with numa logical id
	cpu_bitset_t visited_cpus;
	cpu_bitset_init(&visited_cpus, NR_CPUS);
	int cpu_sid;
	CPU_BITSET_FOREACH (valid_cpus, cpu_sid) {
		int numa_system = numa_node_of_cpu(cpu_sid); // notice this is libnuma
		if (numa_system < 0)
			nosv_abort("Internal error: Could not find NUMA system id for cpu %d", cpu_sid);

		// Check logical id is valid
		int numa_logical = topology_get_logical_id_check(TOPO_NUMA, numa_system);
		if (numa_logical < 0 || numa_logical >= numa_count)
			nosv_abort("Internal error: Could not find NUMA logical id for cpu %d", cpu_sid);

		assert(!cpu_bitset_isset(&visited_cpus, cpu_sid));
		cpu_bitset_set(&visited_cpus, cpu_sid);

		topology_update_cpu_and_parents(cpu_sid, TOPO_NUMA, numa_logical);
	}

	if (cpu_bitset_cmp(&visited_cpus, valid_cpus))
		nosv_abort("Not all cpus from valid cpus bitset were visited when parsing numas from libnuma");

	if ((cpu_bitset_count(topo_lvl_sid_bitset(TOPO_NUMA)) + libnuma_invalid_numas) != libnuma_count)
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
	topology_init_from_config(TOPO_NUMA, valid_cpus, &numa_nodes, 0);
	free(cpulist);
}

static inline void topology_init_numa(cpu_bitset_t *valid_cpus)
{
	if (nosv_config.topology_numa_nodes.n >= 1) { // If more than 1, enable numa from config
		topology_init_from_config(TOPO_NUMA, valid_cpus, &nosv_config.topology_numa_nodes, 0);
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
	topology->per_level_domains[TOPO_CPU] = salloc(sizeof(topo_domain_t) * cnt, 0);
	cpumanager = salloc(sizeof(cpumanager_t) + cnt * sizeof(cpu_t), 0);
	st_config.config->cpumanager_ptr = cpumanager;
	topology->per_level_count[TOPO_CPU] = cnt;
	assert(cnt <= NR_CPUS);

	// Find out maximum CPU id
	int maxcpu = cpu_bitset_fls(valid_cpus);
	assert(maxcpu >= 0);

	// Inform the instrumentation of all available CPUsvalid_cpus
	instr_cpu_count(cnt, maxcpu);

	// Init the system to logical array to -1
	topology_init_domain_s_to_l(TOPO_CPU, NR_CPUS);

	// Save bitset in topology struct
	*topo_lvl_sid_bitset(TOPO_CPU) = *valid_cpus;

	// Init pids_cpus array
	cpumanager->pids_cpus = salloc(sizeof(int) * cnt, 0);

	int cpu_logical = 0;
	int cpu_system;
	CPU_BITSET_FOREACH (valid_cpus, cpu_system) {
		topology_init_domain(TOPO_CPU, cpu_system, cpu_logical);
		cpumanager->cpus[cpu_logical].cpu_domain = topo_dom_ptr(TOPO_CPU, cpu_logical);

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
	int maxsize = 100 * topo_lvl_cnt(TOPO_CORE)															   // core lines
				  + 35 * topo_lvl_cnt(TOPO_CPU)															   // HT lines
				  + 40 * topo_lvl_cnt(TOPO_COMPLEX_SET) + topo_lvl_cnt(TOPO_CPU) * 4 // complex set lines
				  + topo_lvl_cnt(TOPO_NUMA) * 40 + topo_lvl_cnt(TOPO_CPU) * 4 + 200; // NUMA lines
	char *msg = malloc(maxsize * sizeof(char));

	int offset = snprintf(msg, maxsize, "NOSV: Printing locality domains");
	if (offset >= maxsize)
		nosv_abort("Failed to format locality domains");

	snprintf_check(msg, offset, maxsize, "\nNOSV: NODE: 1");

	snprintf_check(msg, offset, maxsize, "\nNOSV: NUMA: system cpus contained in each numa node");
	for (int lid = 0; lid < topo_lvl_cnt(TOPO_NUMA); lid++) {
		topo_domain_t *numa = topo_dom_ptr(TOPO_NUMA, lid);
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
	for (int lid = 0; lid < topo_lvl_cnt(TOPO_COMPLEX_SET); lid++) {
		topo_domain_t *ccx = topo_dom_ptr(TOPO_COMPLEX_SET, lid);
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
	for (int lid = 0; lid < topo_lvl_cnt(TOPO_CORE); lid++) {
		topo_domain_t *core = topo_dom_ptr(TOPO_CORE, lid);
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
	for (int lid = 0; lid < topo_lvl_cnt(TOPO_CPU); lid++) {
		cpu_t *cpu = &cpumanager->cpus[lid];
		snprintf_check(msg, offset, maxsize, "\nNOSV: \t");
		snprintf_check(msg, offset, maxsize, "cpu(logic=%d, system=%d)", cpu_lid(cpu), cpu_sid(cpu));
	}

	nosv_warn("%s", msg);
	free(msg);
}

// Asserts parent is set in domain structure
static inline void topology_assert_parent_is_set(nosv_topo_level_t child, nosv_topo_level_t parent)
{
	assert(child >= TOPO_NUMA && child <= TOPO_CPU);
	assert(parent >= TOPO_NODE && parent <= TOPO_CORE);

	topo_domain_t *arr = topo_lvl_doms(child);
	for (int i = 0; i < topology->per_level_count[child]; ++i) {
		if (arr[i].parents[parent] < 0) {
			nosv_abort("parent %s not set for %s with idx %d. Check initialization of %s",
				topo_lvl_name(parent), topo_lvl_name(child), arr[i].system_id, topo_lvl_name(parent));
		}
	}
}

// Asserts that for every sibling domain, the parents are the same
static inline void topology_assert_siblings_have_same_parent(void)
{
	for (int level = TOPO_CORE; level >= TOPO_NUMA; level--) {
		topo_domain_t *dom_arr = topo_lvl_doms(level);
		for (int parent = level - 1; parent >= TOPO_NUMA; parent--) {
			cpu_bitset_t cpus_visited;
			cpu_bitset_init(&cpus_visited, NR_CPUS);

			for (int i = 0; i < topology->per_level_count[level]; ++i) {
				topo_domain_t *domain = &dom_arr[i];
				int cpu_lid;
				int last_parent_lid = TOPO_ID_UNSET;
				int last_cpu_sid = TOPO_ID_UNSET;
				CPU_BITSET_FOREACH (&domain->cpu_lid_mask, cpu_lid) {
					cpu_t *cpu = cpu_ptr(cpu_lid);

					int parent_lid = cpu_parent_lid(cpu, parent);
					assert(parent_lid >= 0);

					// Assert all cpus in this domain have the same parent
					if (last_parent_lid == TOPO_ID_UNSET) {
						last_parent_lid = parent_lid;
						last_cpu_sid = cpu_sid(cpu);
					}

					if (last_parent_lid != parent_lid) {
						nosv_abort("CPU siblings in domain level %s with system ids (%d, %d) have different parent %s (logical:%d != logical:%d). Check config finitializationile for %s",
							topo_lvl_name(level), cpu_sid(cpu), last_cpu_sid, topo_lvl_name(parent), parent_lid, last_parent_lid, topo_lvl_name(parent));
					}
				}
			}
		}
	}
}

static inline void topology_assert_parents_set(void)
{
	for (int child = TOPO_NUMA; child <= TOPO_CPU; child++) {
		for (int parent = TOPO_NODE; parent < child; parent++) {
			topology_assert_parent_is_set(child, parent);
		}
	}
}

void topo_init(int initialize)
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
	for (int d = 0; d < TOPO_LVL_COUNT; ++d) {
		topology->s_to_l[d] = NULL;
		topology->s_max[d] = TOPO_ID_UNSET;
	}

	// Initialize domain levels
	cpu_bitset_t valid_cpus;
	topology_init_cpus(&valid_cpus);
	topology_init_cores(&valid_cpus);
	topology_init_from_config(TOPO_COMPLEX_SET, &valid_cpus, &nosv_config.topology_complex_sets, 1);
	topology_init_numa(&valid_cpus);
	topology_init_node(&valid_cpus);

	topology_assert_parents_set();
	topology_assert_siblings_have_same_parent();

	if (nosv_config.topology_print)
		topology_print();
}

void topo_free(void)
{
	// Free cpumanager
	int cpu_cnt = topo_lvl_cnt(TOPO_CPU);
	sfree(cpumanager->pids_cpus, cpu_cnt, 0);
	sfree(cpumanager, sizeof(cpumanager_t) + cpu_cnt * sizeof(cpu_t), 0);
	cpumanager = NULL;

	// Free topology system to logical id array, and per level domain array
	for (int lvl = 0; lvl < TOPO_LVL_COUNT; ++lvl) {
		// Per level, free first system to logical map array
		if (topology->s_to_l[lvl]) {
			sfree(topology->s_to_l[lvl], sizeof(int) * (topo_lvl_max(lvl) + 1), 0);
			topology->s_to_l[lvl] = NULL;
		}
		// Per level, free domain array
		if (topology->per_level_domains[lvl]) {
			sfree(topology->per_level_domains[lvl], sizeof(topo_domain_t) * topo_lvl_cnt(lvl), 0);
			topology->per_level_domains[lvl] = NULL;
		}
	}

	// Free topology main pointer
	sfree(topology, sizeof(topology_t), 0);
	topology = NULL;
}

int topo_get_default_aff(char **out)
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

void cpu_mark_free(cpu_t *cpu)
{
	cpumanager->pids_cpus[cpu_lid(cpu)] = -1;

	// A CPU just went idle
	monitoring_cpu_idle(cpu_lid(cpu));
}

void cpu_transfer(int destination_pid, cpu_t *cpu, task_execution_handle_t handle)
{
	assert(cpu);
	cpumanager->pids_cpus[cpu_lid(cpu)] = destination_pid;

	// Wake up a worker from another PID to take over
	worker_wake_idle(destination_pid, cpu, handle);
}

void cpu_affinity_reset(void)
{
	instr_affinity_set(-1);
	cpu_set_t glibc_all_set;
	int cpu;
	CPU_ZERO(&glibc_all_set);
	CPU_BITSET_FOREACH (topo_lvl_sid_bitset(TOPO_CPU), cpu) {
		CPU_SET(cpu, &glibc_all_set);
	}
	bypass_sched_setaffinity(0, sizeof(glibc_all_set), &glibc_all_set);
}

cpu_t *cpu_pop_free(int pid)
{
	for (int i = 0; i < topo_lvl_cnt(TOPO_CPU); ++i) {
		if (cpumanager->pids_cpus[i] == -1) {
			cpumanager->pids_cpus[i] = pid;

			// A CPU is going to become active
			monitoring_cpu_active(i);

			return cpu_ptr(i);
		}
	}

	return NULL;
}

/* Public nOS-V API */
/* Generic Topology API */

int nosv_get_num_domains(nosv_topo_level_t level)
{
	int ret = topo_lvl_cnt(level);
	if (ret < 0)
		return NOSV_ERR_UNKNOWN;

	return ret;
}

int *nosv_get_available_domains(nosv_topo_level_t level)
{
	int *ret = topo_lvl_sid_arr(level);
	assert(ret);
	return ret;
}

int nosv_get_current_logical_domain(nosv_topo_level_t level)
{
	if (!worker_is_in_task())
		return NOSV_ERR_OUTSIDE_TASK;

	cpu_t *cpu = cpu_ptr(cpu_get_current());
	assert(cpu);

	int dom_lid = cpu_parent_lid(cpu, level);
	if (dom_lid < 0)
		return NOSV_ERR_UNKNOWN;

	return dom_lid;
}

int nosv_get_current_system_domain(nosv_topo_level_t level)
{
	if (!worker_is_in_task())
		return NOSV_ERR_OUTSIDE_TASK;

	cpu_t *cpu = cpu_ptr(cpu_get_current());
	assert(cpu);

	int dom_lid = cpu_parent_lid(cpu, level);
	if (dom_lid < 0)
		return NOSV_ERR_UNKNOWN;

	int sid = topo_dom_sid(level, dom_lid);
	if (sid < 0)
		return NOSV_ERR_UNKNOWN;

	return sid;
}

int nosv_get_num_cpus_in_domain(nosv_topo_level_t level, int sid)
{
	int lid = topo_dom_lid(level, sid);
	if (lid < 0)
		return NOSV_ERR_INVALID_PARAMETER;

	cpu_bitset_t *cpus = topo_dom_cpu_sid_bitset(level, lid);
	assert(cpus);
	return cpu_bitset_count(cpus);
}

/* CPU Topology API */

int nosv_get_num_cpus(void)
{
	return nosv_get_num_domains(TOPO_CPU);
}

int *nosv_get_available_cpus(void)
{
	return nosv_get_available_domains(TOPO_CPU);
}

int nosv_get_current_logical_cpu(void)
{
	return nosv_get_current_logical_domain(TOPO_CPU);
}

int nosv_get_current_system_cpu(void)
{
	return nosv_get_current_system_domain(TOPO_CPU);
}

/* NUMA Topology API */

int nosv_get_num_numa_nodes(void)
{
	return nosv_get_num_domains(TOPO_NUMA);
}

int *nosv_get_available_numa_nodes(void)
{
	return nosv_get_available_domains(TOPO_NUMA);
}

int nosv_get_current_logical_numa_node(void)
{
	return nosv_get_current_logical_domain(TOPO_NUMA);
}

int nosv_get_current_system_numa_node(void)
{
	return nosv_get_current_system_domain(TOPO_NUMA);
}

int nosv_get_system_numa_id(int logical_numa_id)
{
	return topo_dom_sid(TOPO_NUMA, logical_numa_id);
}

int nosv_get_logical_numa_id(int system_numa_id)
{
	return topo_dom_lid(TOPO_NUMA, system_numa_id);
}

int nosv_get_num_cpus_in_numa(int system_numa_id)
{
	return nosv_get_num_cpus_in_domain(TOPO_NUMA, system_numa_id);
}

#undef snprintf_check
