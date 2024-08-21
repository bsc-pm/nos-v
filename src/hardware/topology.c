/*
    This file is part of nOS-V and is licensed under the terms contained in the COPYING file.

    Copyright (C) 2021-2023 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <errno.h>
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
#include "scheduler/cpubitset.h"
#include "support/affinity.h"

#define SYS_CPU_PATH "/sys/devices/system/cpu"

thread_local int __current_cpu = -1;
__internal cpumanager_t *cpumanager;
__internal topology_t *topology;

// Init system id to logical id mapping. Sets all values to -1, and inits topology->s_max[level]
static inline int* topology_init_domain_s_to_l(nosv_topo_level_t level, int max)
{
    int size = max + 1;
    int **s_to_l = &topology->s_to_l[level];
    assert(*s_to_l == NULL); // assert we have not initialized this level yet
    *s_to_l = salloc(sizeof(int) * size, 0);
    for (int i = 0; i < size; ++i) {
        (*s_to_l)[i] = -1;
    }
    topology->s_max[level] = max;
    return *s_to_l;
}

// Infer real system core id, which we define as the first cpu sibling of the core. Cannot use core_id sysfs value as it is not unique in a system
static inline int topology_parse_coreid_from_sysfs(int cpu_sid)
{
    // First, open thread_siblings file and read content
    char siblings_filename[PATH_MAX];
    int would_be_size = snprintf(siblings_filename, PATH_MAX, SYS_CPU_PATH "/cpu%d/topology/thread_siblings_list", cpu_sid);
    if (would_be_size >= PATH_MAX) {
        nosv_abort("snprintf failed to format siblings_filename string. String too big with size %d", would_be_size);
    }
    errno = 0;
    FILE *siblings_fp = fopen(siblings_filename, "r");
    if (!siblings_fp || errno != 0) {
        nosv_abort("Couldn't open cpu thread siblings list file %s", siblings_filename);
    }
    char thread_siblings_str[40];
    fgets(thread_siblings_str, 40, siblings_fp);
    fclose(siblings_fp);
    // Check closed correctly
    if (errno != 0) {
        nosv_abort("Couldn't close cpu thread siblings list file %s", siblings_filename);
    }

    // Artificially shorten the string by replacing the '-' and ',' characters
    // with a '\0'.
    // We are only interested in the first value of the sequence.
    char *first_dash = strchrnul(thread_siblings_str, '-');
    if (!first_dash) { // Note: It does not matter if there is no '-' in the string
      nosv_abort("Error parsing thread_siblings string in file %s", siblings_filename);
    } else {
        *first_dash = '\0';
    }
    char *first_comma = strchrnul(thread_siblings_str, ',');
    if (!first_comma) { // Note: It does not matter if there is no ',' in the string
      nosv_abort("Error parsing thread_siblings string in file %s", siblings_filename);
    } else {
        *first_comma = '\0';
    }

    // Lastly, parse the first thread sibling value
    char *first_int_str = thread_siblings_str;
    char *first_int_endptr;
    errno = 0;
    int coreid = strtol(first_int_str, &first_int_endptr, 10);
    if (errno != 0) {
      nosv_abort("Error parsing thread_siblings string in file %s", siblings_filename);
    }
    return coreid;
}

// Returns the topo_domain_t struct for the given level and logical id. Does not accept levels NOSV_TOPO_LEVEL_{NODE,CPU}
topo_domain_t *topology_get_domain(nosv_topo_level_t level, int logical_id)
{
    assert(level >= NOSV_TOPO_LEVEL_NUMA && level <= NOSV_TOPO_LEVEL_CORE);
    assert(logical_id >= 0);

    return &topology->per_domain_array[level-1][logical_id];
}

// Returns the logical id given the topology level and system id
int topology_get_logical(nosv_topo_level_t level, int system_id)
{
    assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
    if (topology->s_max[level] < 0) {
        return TOPO_ID_UNSET; // Level not yet initialized
    }

    if (system_id > topology->s_max[level]) {
        nosv_abort("system_id %d is larger than the maximum system_id %d for topology level %s", system_id, topology->s_max[level], nosv_topo_level_names[level]);
    }

    assert(topology->s_to_l[level][system_id] >= -1);
    return topology->s_to_l[level][system_id];
}

// Returns the array of topo_domain_t structs for the given topology level.
static inline topo_domain_t* topology_get_level_domains(nosv_topo_level_t level)
{
    assert(level >= NOSV_TOPO_LEVEL_NUMA && level <= NOSV_TOPO_LEVEL_CORE);
    return topology->per_domain_array[level-1];
}

// Returns the system id given the topology level and logical id
int topology_get_system(nosv_topo_level_t level, int logical_id)
{
    assert(level >= NOSV_TOPO_LEVEL_NODE && level <= NOSV_TOPO_LEVEL_CPU);
    if (topology->s_max[level] < 0) {
        return TOPO_ID_UNSET; // Level not yet initialized
    }
    if (logical_id >= topology->per_domain_count[level] || logical_id < 0) {
        return TOPO_ID_DISABLED;
    }

    topo_domain_t *domains = topology_get_level_domains(level);
    return domains[logical_id].system_id;
}

// Returns the number of domains in the given topology level
int topology_get_level_count(nosv_topo_level_t d)
{
    assert(d >= NOSV_TOPO_LEVEL_NODE && d <= NOSV_TOPO_LEVEL_CPU);
    if (d == NOSV_TOPO_LEVEL_NODE)
        return 1;

    assert(topology->per_domain_count[d] >= 1);
    return topology->per_domain_count[d];
}

// Returns the name (char array) of the given topology level
const char *topology_get_level_name(nosv_topo_level_t level)
{
    assert(level >= NOSV_TOPO_LEVEL_NODE && level < NOSV_TOPO_LEVEL_COUNT);
    return nosv_topo_level_names[level];
}

// Returns the logical id of the parent in the topology level specified of the given cpu
int cpu_get_parent_logical_id(cpu_t *cpu, nosv_topo_level_t level)
{
    assert(level >= NOSV_TOPO_LEVEL_NUMA && level <= NOSV_TOPO_LEVEL_CORE);
    return cpu->parents[level - 1];
}

// Returns the cpu_bitset_t of logical cpus for the domain (given by level and lid)
cpu_bitset_t* topology_get_domain_cpu_logical_mask(nosv_topo_level_t level, int lid)
{
    assert(level >= NOSV_TOPO_LEVEL_NUMA && level <= NOSV_TOPO_LEVEL_CORE);
    topo_domain_t *domains = topology_get_level_domains(level);
    return &(domains[lid].cpu_lid_mask);
}

// Returns the cpu_bitset_t of system cpus for the domain (given by level and lid)
cpu_bitset_t* topology_get_domain_cpu_system_mask(nosv_topo_level_t level, int lid)
{
    assert(level >= NOSV_TOPO_LEVEL_NUMA && level <= NOSV_TOPO_LEVEL_CORE);
    topo_domain_t *domains = topology_get_level_domains(level);
    return &(domains[lid].cpu_sid_mask);
}

// Returns the logical id of the parent in the topology level specified of the given domain (specified by level and lid)
void topology_domain_set_parent(nosv_topo_level_t level, int lid, nosv_topo_level_t parent, int parent_logical)
{
    assert(level >= NOSV_TOPO_LEVEL_NUMA);
    assert(level <= NOSV_TOPO_LEVEL_CPU);
    assert(parent >= NOSV_TOPO_LEVEL_NUMA);
    assert(parent <= NOSV_TOPO_LEVEL_CORE);
    assert(level != parent);

    if (level == NOSV_TOPO_LEVEL_CPU) {
        cpumanager->cpus[lid].parents[parent - 1] = parent_logical;
    } else {
        topo_domain_t *domain = topology_get_domain(level, lid);
        domain->parents[parent - 1] = parent_logical;
    }
}

void topology_domain_set_ids(nosv_topo_level_t level, int system_id, int logical_id)
{
    assert(level >= NOSV_TOPO_LEVEL_NUMA);
    assert(level <= NOSV_TOPO_LEVEL_CPU);
    assert(system_id >= 0);
    assert(logical_id >= 0);

    topology->s_to_l[level][system_id] = logical_id;
    if (level == NOSV_TOPO_LEVEL_CPU) {
        cpumanager->cpus[logical_id].logical_id = logical_id;
        cpumanager->cpus[logical_id].system_id = system_id;
    } else {
        topo_domain_t *domain = topology_get_domain(level, logical_id);
        domain->system_id = system_id;
        domain->parents[level - 1] = logical_id;
    }
}

// Sets system to logical id mapping, inits parents and logical id, and inits cpu bitsets
static inline void topology_init_domain(nosv_topo_level_t level, int system_id, int logical_id)
{
    topology_domain_set_ids(level, system_id, logical_id);
    for (int l = NOSV_TOPO_LEVEL_NUMA; l < NOSV_TOPO_LEVEL_CPU; l++) {
        if (l == level) {
            // Init the logical id
            continue;
        } else if (l == NOSV_TOPO_LEVEL_CPU && level != NOSV_TOPO_LEVEL_CPU) {
            // topo_domain_t does not have a "parent cpu member", so omit
            continue;
        } else {
            // Set as disabled the ids lower in the hierarchy, and to unset the ones higher
            int value = l > level ? TOPO_ID_DISABLED : TOPO_ID_UNSET;
            topology_domain_set_parent(level, logical_id, l, value);
        }
    }

    // Cpus do not have bitsets
    if (level == NOSV_TOPO_LEVEL_CPU) {
        CPU_ZERO(&cpumanager->cpus[logical_id].cpuset);
        CPU_SET(system_id, &cpumanager->cpus[logical_id].cpuset);
    } else {
        topo_domain_t *dom = topology_get_domain(level, logical_id);
        cpu_bitset_init(&(dom->cpu_sid_mask), NR_CPUS);
        cpu_bitset_init(&(dom->cpu_lid_mask), NR_CPUS);
    }
}

// Updates cpus, cores and complex sets domains setting the value of parent domain specified with dom_level and dom_logical
static inline void update_cpu_and_parents(int cpu_system, nosv_topo_level_t parent_level, int parent_logical)
{
    assert(parent_level != NOSV_TOPO_LEVEL_CPU && parent_level != NOSV_TOPO_LEVEL_NODE);

    topo_domain_t *dom = topology_get_domain(parent_level, parent_logical);

    // Set cpu masks on parent
    cpu_bitset_set(&(dom->cpu_sid_mask), cpu_system);
    int cpu_logical = topology_get_logical(NOSV_TOPO_LEVEL_CPU, cpu_system);
    cpu_bitset_set(&(dom->cpu_lid_mask), cpu_logical);

    // Set parent logical id in cpu struct
    cpu_t *cpu = cpu_get(cpu_logical);
    topology_domain_set_parent(NOSV_TOPO_LEVEL_CPU, cpu_logical, parent_level, parent_logical);

    // Update all levels below the parent and above the cpu with the parent logical id
    for (int p_level = NOSV_TOPO_LEVEL_CORE; p_level > parent_level; p_level--) {
        int p_logical = cpu_get_parent_logical_id(cpu, p_level);
        topology_domain_set_parent(p_level, p_logical, parent_level, parent_logical);
    }
}

// Inits core topo_domain_t structs without the numa and complex set info
static inline void topology_init_cores(cpu_bitset_t const* const valid_cpus)
{
    // Init system to logical array
    topology_init_domain_s_to_l(NOSV_TOPO_LEVEL_CORE, NR_CPUS);

    cpu_bitset_t *valid_cores = &topology->valid_cores;
    cpu_bitset_init(valid_cores, NR_CPUS);
    topology->cores = malloc(sizeof(topo_domain_t) * cpu_bitset_count(valid_cpus));

    int cpu_system = 0;
    int core_logical = 0;
    CPU_BITSET_FOREACH(valid_cpus, cpu_system) {
        int core_system = topology_parse_coreid_from_sysfs(cpu_system);

        // Init core
        if (!cpu_bitset_isset(valid_cores, core_system)) {
            cpu_bitset_set(valid_cores, core_system);

            topology_init_domain(NOSV_TOPO_LEVEL_CORE, core_system, core_logical);

            core_logical++;
        }

        // Get core logic id, irrespective of whether or not is the first time core_system is found or not
        int _core_logical = topology_get_logical(NOSV_TOPO_LEVEL_CORE, core_system);

       update_cpu_and_parents(cpu_system, NOSV_TOPO_LEVEL_CORE, _core_logical);
    }

    topology->core_count = cpu_bitset_count(valid_cores);
    assert(core_logical == topology->core_count);

    topo_domain_t *shmem_cores = salloc(sizeof(topo_domain_t) * topology->core_count, 0);
    memmove(shmem_cores, topology->cores, sizeof(topo_domain_t) * topology->core_count);
    free(topology->cores);
    topology->cores = shmem_cores;
}

// Returns the logical id of the parent in the topology level specified of the given domain (specified by level and lid)
int topology_domain_get_parent(nosv_topo_level_t level, int lid, nosv_topo_level_t parent)
{
    assert(level != NOSV_TOPO_LEVEL_NODE);

    if (parent == NOSV_TOPO_LEVEL_NODE) {
        return 0;
    }

    if (level == NOSV_TOPO_LEVEL_CPU) {
        return cpumanager->cpus[lid].parents[parent - 1];
    } else if (level == NOSV_TOPO_LEVEL_CORE) {
        return topology->cores[lid].parents[parent - 1];
    } else if (level == NOSV_TOPO_LEVEL_COMPLEX_SET) {
        return topology->complex_sets[lid].parents[parent - 1];
    } else if (level == NOSV_TOPO_LEVEL_NUMA) {
        return topology->numas[lid].parents[parent - 1];
    } else {
        nosv_abort("Invalid domain");
    }
}

// Iterate over cpus inside core and update complex set info
static inline void topology_set_complex_set_in_core_cpus(int core_logical, int cs_lid, const cpu_bitset_t *valid_cpus)
{
    cpu_bitset_t *core_cpus_sid = topology_get_domain_cpu_system_mask(NOSV_TOPO_LEVEL_CORE, core_logical);
    int cpu_sid;
    CPU_BITSET_FOREACH(core_cpus_sid, cpu_sid) {
        if (cpu_bitset_isset(valid_cpus, cpu_sid)) {

            update_cpu_and_parents(cpu_sid, NOSV_TOPO_LEVEL_COMPLEX_SET, cs_lid);
        }
    }
}

static inline bool array_has_valid_cpus(const generic_array_t *array, cpu_bitset_t *valid_cpus)
{
    uint64_t *cpu_idx = (uint64_t *) array->items;
    for (int i = 0; i < array->n; ++i) {
        if (cpu_bitset_isset(valid_cpus, cpu_idx[i])) {
            return true;
        }
    }
    return false;
}

// Inits topo_domain_t structures without the numa info 
static inline void topology_init_complex_sets(cpu_bitset_t *valid_cpus, cpu_bitset_t *valid_cores)
{
    int core_cnt = cpu_bitset_count(valid_cores);
    int maxcs = core_cnt - 1;
    // Use the relevant configuration option
    int config_cs_count = nosv_config.affinity_complex_sets.n;

    topology_init_domain_s_to_l(NOSV_TOPO_LEVEL_COMPLEX_SET, maxcs);

    topology->complex_sets = (topo_domain_t *) malloc(sizeof(topo_domain_t) * core_cnt);

    cpu_bitset_t visited_cores;
    cpu_bitset_init(&visited_cores, NR_CPUS);
    generic_array_t *config_cs = (generic_array_t *) nosv_config.affinity_complex_sets.items;

    int cs_logical = 0; // logical id
    int cs_system;
    for (cs_system = 0; cs_system < config_cs_count; ++cs_system) {
        if (config_cs[cs_system].n == 0) {
            nosv_warn("Error parsing config file. All complex sets must have at least 1 ht in the config file.");
        }

        // Ignore complex sets that have no valid cpus
        if (!array_has_valid_cpus(&config_cs[cs_system], valid_cpus)) {
            continue;
        }

        topology_init_domain(NOSV_TOPO_LEVEL_COMPLEX_SET, cs_system, cs_logical);

        cpu_bitset_set(&topology->valid_complex_sets, cs_system);

        uint64_t *system_ids = (uint64_t *) config_cs[cs_system].items;
        for (int j = 0; j < config_cs[cs_system].n; ++j) {
            int cpu_system = (int) system_ids[j];
            int core_system = topology_parse_coreid_from_sysfs(cpu_system);
            if (!cpu_bitset_isset(valid_cores, core_system)) {
                continue;
            }
            if (cpu_bitset_isset(&visited_cores, core_system)) {
                nosv_abort("system core %d configured to be included in complex set %d, but it is already included in another complex set.", core_system, cs_system);
            }
            cpu_bitset_set(&visited_cores, (int)core_system);

            int core_logical = topology_get_logical(NOSV_TOPO_LEVEL_CORE, core_system);
            topology_set_complex_set_in_core_cpus(core_logical, cs_logical, valid_cpus);
        }

        cs_logical++;
    }

    // Now, make a CS for each core that was not visited
    int core_sid;
    CPU_BITSET_FOREACH(valid_cores, core_sid) {
        if (!cpu_bitset_isset(&visited_cores, core_sid)) {
            topology_init_domain(NOSV_TOPO_LEVEL_COMPLEX_SET, cs_system, cs_logical);

            int core_lid = topology_get_logical(NOSV_TOPO_LEVEL_CORE, core_sid);
            topology_set_complex_set_in_core_cpus(core_lid, cs_logical, valid_cpus);

            cs_system++;
            ++cs_logical;
        }
    }

    // Move malloc'd CSs to shared memory
    topo_domain_t *shmem_cs = salloc(sizeof(topo_domain_t) * cs_logical, 0);
    memmove(shmem_cs, topology->complex_sets, sizeof(topo_domain_t) * cs_logical);
    free(topology->complex_sets);
    topology->complex_sets = shmem_cs;
    topology->complex_set_count = cs_logical;

    assert(cs_logical <= maxcs+1);
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

    if (config_numa_count > cpus_cnt) {
        nosv_warn("Error parsing config file. Number of numas (%d) is greater than number of cpus (%d).", config_numa_count, cpus_cnt);
    }

    topology->numas = (topo_domain_t *) malloc(sizeof(topo_domain_t) * config_numa_count);

    cpu_bitset_t visited_cpus;
    cpu_bitset_init(&visited_cpus, NR_CPUS);
    generic_array_t *config_numas = (generic_array_t *) all_numa_config->items;

    int numa_logical = 0;
    for (int numa_system = 0; numa_system < config_numa_count; ++numa_system) {
        if (config_numas[numa_system].n == 0) {
            nosv_warn("Error parsing config file. All complex sets must have at least 1 ht in the config file.");
        }

        // Ignore numas that have no valid cpus
        if (!array_has_valid_cpus(&config_numas[numa_system], valid_cpus)) {
            continue;
        }

        cpu_bitset_set(&topology->valid_numas, numa_system);

        topology_init_domain(NOSV_TOPO_LEVEL_NUMA, numa_system, numa_logical);

        uint64_t *cpu_idx = (uint64_t *) config_numas[numa_system].items;
        for (int j = 0; j < config_numas[numa_system].n; ++j) {
            int cpu_system = (int) cpu_idx[j];
            if (!cpu_bitset_isset(valid_cpus, cpu_system)) {
                continue;
            }

            // Check vailidity of cpu_system
            if (cpu_bitset_isset(&visited_cpus, cpu_system)) {
                nosv_abort("cpu %d configured to be included in numa %d, but cpu is already included in another numa.", cpu_system, numa_system);
            }
            if (!cpu_bitset_isset(valid_cpus, cpu_system)) {
                continue;
            }

            cpu_bitset_set(&visited_cpus, cpu_system);

            update_cpu_and_parents(cpu_system, NOSV_TOPO_LEVEL_NUMA, numa_logical);
        }

        numa_logical++;
    }

    if (cpu_bitset_cmp(&visited_cpus, &topology->valid_cpus)) {
        nosv_abort("Error: Numa config has more cpus or less cpus than all the valid cpus. These sets must be equal. This should not happen.");
    }

    // Move malloc'd CSs to shared memory
    topo_domain_t *shmem_numa = salloc(sizeof(topo_domain_t) * numa_logical, 0);
    memmove(shmem_numa, topology->numas, sizeof(topo_domain_t) * numa_logical);
    free(topology->numas);
    topology->numas = shmem_numa;
    topology->numa_count = numa_logical;
}

// Inits topology->numas, updates cpus, cores and complex sets domains setting numa logical id
static inline void topology_init_numa_from_libnuma(cpu_bitset_t *valid_cpus)
{
    // Use numa_all_nodes_ptr as that contains only the nodes that are actually available,
    // not all configured. On some machines, some nodes are configured but unavailable.
    topology->numa_count = numa_bitmask_weight(numa_all_nodes_ptr);
    topology_init_domain_s_to_l(NOSV_TOPO_LEVEL_NUMA, topology->numa_count);
    int numa_max = numa_max_node();
    if (numa_max <= 0) {
        nosv_abort("Error: Number of numa nodes is %d, which is invalid.", numa_max);
    }
    cpu_bitset_init(&topology->valid_numas, NR_CPUS);

    topology->numas = (topo_domain_t *) salloc(sizeof(topo_domain_t) * topology->numa_count, 0);	

    int logical_id = 0;
     for (int i = 0; i <= numa_max; ++i) {
        if (numa_bitmask_isbitset(numa_all_nodes_ptr, i)) {
            cpu_bitset_set(&topology->valid_numas, logical_id);

            topology_init_domain(NOSV_TOPO_LEVEL_NUMA, i, logical_id);

            logical_id++;
        }
    }

    // Update cpus, cores and complex sets with numa logical id
    cpu_bitset_t visited_cpus;
    cpu_bitset_init(&visited_cpus, NR_CPUS);
    int cpu_sid;
    CPU_BITSET_FOREACH(valid_cpus, cpu_sid) {
        int numa_system = numa_node_of_cpu(cpu_sid); // notice this is libnuma
        if (numa_system < 0) {
            nosv_abort("Internal error: Could not find NUMA system id for cpu %d", cpu_sid);
        }

        // Check logical id is valid
        int numa_logical = topology_get_logical(NOSV_TOPO_LEVEL_NUMA, numa_system);
        if (numa_logical < 0 || numa_logical >= topology->numa_count) {
            nosv_abort("Internal error: Could not find NUMA logical id for cpu %d", cpu_sid);
        }

        assert(!cpu_bitset_isset(&visited_cpus, cpu_sid));
        cpu_bitset_set(&visited_cpus, cpu_sid);

        update_cpu_and_parents(cpu_sid, NOSV_TOPO_LEVEL_NUMA, numa_logical);
    }

    if (cpu_bitset_cmp(&visited_cpus, valid_cpus)) {
        nosv_abort("Not all cpus from valid cpus bitset were visited when parsing numas from libnuma");
    }

    if (cpu_bitset_count(&topology->valid_numas) != topology->numa_count) {
        nosv_abort("Not all numas from libnuma were visited when parsing numas");
    }
}

static inline void topology_init_numa(cpu_bitset_t *valid_cpus)
{
    if (nosv_config.affinity_numa_nodes.n >= 1) { // If more than 1, enable numa from config
        topology_init_numa_from_config(valid_cpus, &nosv_config.affinity_numa_nodes);
    } else if (numa_available() != -1) {
            topology_init_numa_from_libnuma(valid_cpus);
    } else {
        // Create numa config with all cpus in one numa
        generic_array_t numa_nodes;
        numa_nodes.n = 1;

        // Alloc generic array for only one numa
        generic_array_t *numa0 = (generic_array_t *)malloc(sizeof(generic_array_t));
        numa0->n = cpu_bitset_count(valid_cpus);
        numa0->items = malloc(sizeof(uint64_t) * numa0->n);
        // Set numa nodes items to point to the only generic array it will have
        numa_nodes.items = (void*)numa0;

        // Iterate over valid cpus and add them to the only one numa array
        uint64_t *cpuids_arr = (uint64_t *)numa0->items;
        int cpu_system;
        int i = 0;
        CPU_BITSET_FOREACH(valid_cpus, cpu_system) {
            cpuids_arr[i++] = cpu_system;
        }

        // Lastly, init numa using generic array
        topology_init_numa_from_config(valid_cpus, &numa_nodes);

        free(numa0->items);
        free(numa0);
    }
}

// Parse a CPU set which is specified in separation by "-" and "," into a CPU set
static inline void cpu_parse_set(cpu_set_t *set, char *string_to_parse)
{
    CPU_ZERO(set);

    char *tok = strtok(string_to_parse, ",");
    while(tok) {
        int first_id, last_id;
        int ret = sscanf(tok, "%d-%d", &first_id, &last_id);
        if (ret == 1)
            last_id = first_id;
        else if (ret == 0)
            nosv_abort("Could not parse cpu list");

        for (int i = first_id; i <= last_id; ++i)
            CPU_SET(i, set);

        tok = strtok(NULL, ",");
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

    cpu_parse_set(&set, online_mask);

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
        if (binding[0] != '0' || (binding[1] != 'x' && binding[1] != 'X')) {
            nosv_abort("invalid binding mask");
        }

        const int len = strlen(binding);
        for (int c = len-1, b = 0; c >= 2; --c, b += 4) {
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

            if (number & 0x1) CPU_SET(b + 0, &glibc_cpuset);
            if (number & 0x2) CPU_SET(b + 1, &glibc_cpuset);
            if (number & 0x4) CPU_SET(b + 2, &glibc_cpuset);
            if (number & 0x8) CPU_SET(b + 3, &glibc_cpuset);
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
    cpus_get_binding_mask(nosv_config.cpumanager_binding, valid_cpus);

    int cnt = cpu_bitset_count(valid_cpus);
    assert(cnt > 0);

    // The CPU array is located just after the CPU manager, as a flexible array member.
    cpumanager = salloc(sizeof(cpumanager_t) + cnt * sizeof(cpu_t), 0);
    st_config.config->cpumanager_ptr = cpumanager;
    topology->cpu_count = cnt;
    assert(cnt <= NR_CPUS);

    // Find out maximum CPU id
    int maxcpu = cpu_bitset_fls(valid_cpus);
    assert(maxcpu > 0);

    // Inform the instrumentation of all available CPUsvalid_cpus
    instr_cpu_count(cnt, maxcpu);

    // Init the system to logical array to -1
    topology_init_domain_s_to_l(NOSV_TOPO_LEVEL_CPU, NR_CPUS);

    // Save bitset in topology struct
    topology->valid_cpus = *valid_cpus;

    // Init pids_cpus array
    cpumanager->pids_cpus = salloc(sizeof(int) * cnt, 0);

    int cpu_logical = 0;
    int cpu_system;
    CPU_BITSET_FOREACH(valid_cpus, cpu_system) {
        topology_init_domain(NOSV_TOPO_LEVEL_CPU, cpu_system, cpu_logical);

        // Hardware counters
        cpuhwcounters_initialize(&(cpumanager->cpus[cpu_logical].counters));

        // Inform the instrumentation of a new CPU
        instr_cpu_id(cpu_logical, cpu_system);

        // Initialize the mapping as empty
        cpumanager->pids_cpus[cpu_logical] = -1;

        cpu_logical++;
    }
}

void topology_print(void)
{
    int maxsize = 100 * topology->core_count // core lines
        + 35 * topology->cpu_count // HT lines
        + 40 * topology->complex_set_count + topology->cpu_count * 4 // complex set lines
        + topology->numa_count * 40 + topology->cpu_count*4 + 200; // NUMA lines
    char *msg = malloc(maxsize * sizeof(char));

    int would_be_size = snprintf(msg, maxsize, "NOSV: Printing locality domains");
    if (would_be_size >= maxsize) {
        nosv_abort("Failed to format locality domains");
    }

    strcat(msg, "\nNOSV: NODE: 1");

    strcat(msg, "\nNOSV: NUMA: system hts contained in each numa node");
    for (int lid = 0; lid < topology_get_level_count(NOSV_TOPO_LEVEL_NUMA); lid++) {
        strcat(msg, "\nNOSV: \t");
        topo_domain_t *numa = &topology->numas[lid];
        char *numa_str;
        int size = cpu_bitset_count(&numa->cpu_sid_mask);
        nosv_asprintf(&numa_str, "numa(logic=%d, system=%d, num_items=%d) = [", lid, numa->system_id, size);
        strcat(msg, numa_str);
        int count = 0;
        int cpu;
        CPU_BITSET_FOREACH(&numa->cpu_sid_mask, cpu) {
            char *_str;
            nosv_asprintf(&_str, "%d", cpu);

            if (count++ > 0)
                    strcat(msg, ",");
            strcat(msg, _str);
        }
        strcat(msg, "] ");
    }

    strcat(msg, "\nNOSV: CCX: system cpus contained in each core complex");
    for (int lid = 0; lid < topology_get_level_count(NOSV_TOPO_LEVEL_COMPLEX_SET); lid++) {
        strcat(msg, "\nNOSV: \t");
        topo_domain_t *ccx = &topology->complex_sets[lid];
        char *ccx_str;
        int size = cpu_bitset_count(&ccx->cpu_sid_mask);
        nosv_asprintf(&ccx_str, "CCX(logic=%d, system=N/A, num_items=%d) = [", lid, size);
        strcat(msg, ccx_str);
        int count = 0;
        int cpu;
        CPU_BITSET_FOREACH(&ccx->cpu_sid_mask, cpu) {
            char *_str;
            nosv_asprintf(&_str, "%d", cpu);
            if (count++ > 0)
                strcat(msg, ",");
            strcat(msg, _str);
        }
        strcat(msg, "] ");
    }

    strcat(msg, "\nNOSV: CORE: system cpus contained in each core");
    for (int lid = 0; lid < topology_get_level_count(NOSV_TOPO_LEVEL_CORE); lid++) {
        strcat(msg, "\nNOSV: \t");
        topo_domain_t *core = &topology->cores[lid];
        char *core_str;
        int size = cpu_bitset_count(&core->cpu_sid_mask);
        nosv_asprintf(&core_str, "core(logic=%d, system=%d, num_items=%d) = [", lid, core->system_id, size);
        strcat(msg, core_str);
        int count = 0;
        int cpu;
        CPU_BITSET_FOREACH(&core->cpu_sid_mask, cpu) {
            char *_str;
            nosv_asprintf(&_str, "%d", cpu);
            if (count++ > 0)
                strcat(msg, ",");
            strcat(msg, _str);
        }
        strcat(msg, "] ");
    }

    strcat(msg, "\nNOSV: CPU: cpu(logic=lid, system=sid)");
    for (int lid = 0; lid < topology_get_level_count(NOSV_TOPO_LEVEL_CPU); lid++) {
        cpu_t *cpu = &cpumanager->cpus[lid];
        strcat(msg, "\nNOSV: \t");
        char *cpu_str;
        nosv_asprintf(&cpu_str, "cpu(logic=%d, system=%d)", cpu->logical_id, cpu->system_id);
        strcat(msg, cpu_str);
    }

    nosv_warn("%s", msg);
    free(msg);
}
// Asserts parent is set in domain structure
static inline void topology_assert_parent_is_set(nosv_topo_level_t son, nosv_topo_level_t parent)
{
    assert(son >= NOSV_TOPO_LEVEL_COMPLEX_SET && son <= NOSV_TOPO_LEVEL_CPU);
    assert(parent >= NOSV_TOPO_LEVEL_NUMA && parent <= NOSV_TOPO_LEVEL_CORE);

    int p_idx = parent - 1; // The parents array omits the NODE level

    if (son == NOSV_TOPO_LEVEL_CPU) {
        for (int i = 0; i < topology->cpu_count; i++) {
            if (cpumanager->cpus[i].parents[p_idx] < 0) {
                nosv_abort("parent %s not set for domain at level %s with idx %d. Check initialization of %s",
                    topology_get_level_name(parent), topology_get_level_name(son), cpumanager->cpus[i].system_id, topology_get_level_name(parent));
            }
        }
    } else {
        topo_domain_t *arr = NULL;
        if (son == NOSV_TOPO_LEVEL_CORE) {
            arr = topology->cores;
        } else if (son == NOSV_TOPO_LEVEL_COMPLEX_SET) {
            arr = topology->complex_sets;
        } else if (son == NOSV_TOPO_LEVEL_NUMA) {
            arr = topology->numas;
        }

        for (int i = 0; i < topology->per_domain_count[son]; ++i) {
            if (arr[i].parents[p_idx] < 0) {
                nosv_abort("parent %s not set for %s with idx %d. Check initialization of %s",
                    topology_get_level_name(parent), topology_get_level_name(son), arr[i].system_id, topology_get_level_name(parent));
            }
        }
    }
}

// Asserts that for every brother domain, the parents are the same
static inline void topology_assert_siblings_have_same_parent(void)
{
    for (int level = NOSV_TOPO_LEVEL_CORE; level >= NOSV_TOPO_LEVEL_NUMA; level--) {
        topo_domain_t *dom_arr = topology_get_level_domains(level);
        for (int parent = level-1; parent >= NOSV_TOPO_LEVEL_NUMA; parent--) {
            cpu_bitset_t cpus_visited;
            cpu_bitset_init(&cpus_visited, NR_CPUS);

            for (int i = 0; i < topology->per_domain_count[level]; ++i) {
                topo_domain_t *domain = &dom_arr[i];
                int cpu_lid;
                int last_parent_lid = TOPO_ID_UNSET;
                int last_cpu_sid = TOPO_ID_UNSET;
                CPU_BITSET_FOREACH(&domain->cpu_lid_mask, cpu_lid) {
                    cpu_t *cpu = cpu_get(cpu_lid);

                    int parent_lid = cpu_get_parent_logical_id(cpu, parent);
                    assert(parent_lid >= 0);

                    // Assert all cpus in this domain have the same parent
                    if (last_parent_lid == TOPO_ID_UNSET) {
                        last_parent_lid = parent_lid;
                        last_cpu_sid = cpu->system_id;
                    }

                    if (last_parent_lid != parent_lid) {
                        nosv_abort("CPU siblings in domain level %s with system ids (%d, %d) have different parent %s (numa%d != numa%d). Check config finitializationile for %s",
                            topology_get_level_name(level), cpu->system_id, last_cpu_sid, topology_get_level_name(parent), cpu->logical_numa, last_parent_lid, topology_get_level_name(parent));
                    }
                }
            }
        }
    }
}

static inline void topology_assert_parents_set(void) {
    for (int son = NOSV_TOPO_LEVEL_COMPLEX_SET; son <= NOSV_TOPO_LEVEL_CPU; son++) {
        for (int parent = NOSV_TOPO_LEVEL_NUMA; parent < son; parent++) {
            topology_assert_parent_is_set(son, parent);
        }
    }
}

void topology_init(int initialize)
{
    if (!initialize) {
        cpumanager = (cpumanager_t *)st_config.config->cpumanager_ptr;
        assert(cpumanager);
        topology = (topology_t *)st_config.config->topology_ptr;
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

    topology_init_domain_s_to_l(NOSV_TOPO_LEVEL_NODE, 0);
    topology->s_to_l[NOSV_TOPO_LEVEL_NODE][0] = 0;
    topology->s_max[NOSV_TOPO_LEVEL_NODE] = 0;

    cpu_bitset_t valid_cpus;
    topology_init_cpus(&valid_cpus);
    topology_init_cores(&valid_cpus);
    topology_init_complex_sets(&valid_cpus, &topology->valid_cores);
    topology_init_numa(&valid_cpus);

    topology_assert_parents_set();
    topology_assert_siblings_have_same_parent();

    const char *env_print_locality_domains = getenv("NOSV_PRINT_TOPOLOGY");
    if (env_print_locality_domains != NULL && (strcmp(env_print_locality_domains, "TRUE") == 0 || strcmp(env_print_locality_domains, "true") == 0)) {
        topology_print();
    }
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

cpu_t *cpu_get(int cpu)
{
    return &cpumanager->cpus[cpu];
}

cpu_t *cpu_pop_free(int pid)
{
    for (int i = 0; i < topology_get_level_count(NOSV_TOPO_LEVEL_CPU); ++i) {
        if (cpumanager->pids_cpus[i] == -1) {
            cpumanager->pids_cpus[i] = pid;

            // A CPU is going to become active
            monitoring_cpu_active(i);

            return cpu_get(i);
        }
    }

    return NULL;
}

void cpu_set_pid(cpu_t *cpu, int pid)
{
    assert(cpumanager->pids_cpus[cpu->logical_id] < MAX_PIDS);
    cpumanager->pids_cpus[cpu->logical_id] = pid;
}

void cpu_mark_free(cpu_t *cpu)
{
    cpumanager->pids_cpus[cpu->logical_id] = -1;

    // A CPU just went idle
    monitoring_cpu_idle(cpu->logical_id);
}

void cpu_transfer(int destination_pid, cpu_t *cpu, task_execution_handle_t handle)
{
    assert(cpu);
    cpumanager->pids_cpus[cpu->logical_id] = destination_pid;

    // Wake up a worker from another PID to take over
    worker_wake_idle(destination_pid, cpu, handle);
}

void cpu_affinity_reset(void)
{

    instr_affinity_set(-1);
    cpu_set_t glibc_all_set;
    int cpu;
    CPU_ZERO(&glibc_all_set);
    CPU_BITSET_FOREACH(&topology->valid_cpus, cpu) {
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

    cpu_t *cpu = cpu_get(cpu_get_current());
    assert(cpu);

    return cpu->system_id;
}

int nosv_get_num_numa_nodes(void)
{
	return topology->numa_count;
}

int nosv_get_system_numa_id(int logical_numa_id)
{
	if (logical_numa_id >= topology->numa_count)
		return NOSV_ERR_INVALID_PARAMETER;
        
	return topology_get_system(NOSV_TOPO_LEVEL_NUMA, logical_numa_id);
}

int nosv_get_logical_numa_id(int system_numa_id)
{
	return topology_get_logical(NOSV_TOPO_LEVEL_NUMA, system_numa_id);
}

int nosv_get_num_cpus_in_numa(int system_numa_id)
{
	int logical_node = topology_get_logical(NOSV_TOPO_LEVEL_NUMA, system_numa_id);

	return cpu_bitset_count(&topology->numas[logical_node].cpu_sid_mask);
}
