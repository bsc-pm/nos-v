/*
	This file is part of Nanos6 and nOS-V and is licensed under the terms contained in the COPYING file.

	Copyright (C) 2020-2024 Barcelona Supercomputing Center (BSC)
*/

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "common.h"
#include "compiler.h"
#include "defaults.h"
#include "config/config.h"
#include "config/configspec.h"
#include "config/toml.h"
#include "hardware/cpus.h"
#include "hardware/locality.h"

#ifndef INSTALLED_CONFIG_DIR
#error "INSTALLED_CONFIG_DIR should be defined at make time"
#endif

#define CONFIG_SECTION_TOKEN '.'

#define PTR_TO(type, struct_ptr, offset) \
	((type *)(((char *)(struct_ptr)) + (offset)))

#define PTR_TO_ELEM(type, struct_ptr, offset) \
	((type *)(((char *)(struct_ptr)) + (offset) * sizeof(type)))

__internal rt_config_t nosv_config;

static char config_path[MAX_CONFIG_PATH];
static char default_config_path[MAX_CONFIG_PATH];

extern char **environ;

// To add a config option, set its default value here
static inline void config_init(rt_config_t *config)
{
	config->sched_batch_size = SCHED_BATCH_SIZE;
	config->sched_quantum_ns = SCHED_QUANTUM_NS;
	config->sched_cpus_per_queue = SCHED_MPSC_CPU_BATCH;
	config->sched_in_queue_size = SCHED_IN_QUEUE_SIZE;
	config->sched_immediate_successor = 1;

	// Use strdup for strings by default?
	config->shm_name = strdup(SHM_NAME);
	assert(config->shm_name);
	config->shm_isolation_level = strdup(SHM_ISOLATION_LEVEL);
	assert(config->shm_isolation_level);
	config->shm_size = SHM_SIZE;
	config->shm_start = SHM_START_ADDR;

	config->governor_policy = strdup("hybrid");
	config->governor_spins = 10000;

	config->cpumanager_binding = strdup(CPUMANAGER_BINDING);
	config->affinity_default = strdup(AFFINITY_DEFAULT);
	config->affinity_default_policy = strdup(AFFINITY_DEFAULT_POLICY);
	config->affinity_compat_support = AFFINITY_COMPAT_SUPPORT;
	config->affinity_numa_nodes.n = 0;

	config->debug_dump_config = 0;

	config->hwcounters_verbose = HWCOUNTERS_VERBOSE;
	config->hwcounters_backend = strdup(HWCOUNTERS_BACKEND);

	uint64_t num_papi_events = 2;
	config->hwcounters_papi_events.num_strings = num_papi_events;
	config->hwcounters_papi_events.strings = (char **) malloc(num_papi_events * sizeof(char *));
	for (int i = 0; i < num_papi_events; ++i) {
		config->hwcounters_papi_events.strings[i] = (char *) malloc(sizeof(char) * 16);
	}
	sprintf(config->hwcounters_papi_events.strings[0], "PAPI_TOT_INS");
	sprintf(config->hwcounters_papi_events.strings[1], "PAPI_TOT_CYC");

	config->turbo_enabled = 0;

	config->monitoring_enabled = 0;
	config->monitoring_verbose = 0;

	config->instrumentation_version = strdup(INSTR_DEFAULT_VERSION);

	config->thread_stack_size = THREAD_STACK_SIZE;

	config->ovni_level = 2;
	config->ovni_events.num_strings = 0;
	config->ovni_events.strings = NULL;
	config->ovni_kernel_ringsize = INSTR_OVNI_KERNEL_RINGSIZE;
}

#define sanity_check(cond, explanation)                             \
	if (!(cond)) {                                                  \
		nosv_warn("Check \"%s\" failed: %s", #cond, explanation);   \
		ret = 1;                                                    \
	}

#define sanity_check_str(str, explanation, ...) 						\
	do {                                        						\
		const char *_arr[] = {__VA_ARGS__};      						\
		int _eq = 0;                             						\
		for (int _i = 0; _i < (sizeof(_arr) / sizeof(_arr[0])); ++_i) 	\
			_eq |= !strcmp((str), _arr[_i]); 							\
		sanity_check(_eq, (explanation)) 								\
	} while (0)

// Sanity checks for configuration options should be here
static inline int config_check(rt_config_t *config)
{
	int ret = 0;

	sanity_check(config->sched_batch_size > 0, "Scheduler batch size should be more than 0");
	sanity_check(config->sched_cpus_per_queue > 0, "CPUs per queue cannot be lower than 1");
	sanity_check(config->sched_in_queue_size > 0, "In scheduler queues cannot be of length 0");

	// Remember empty strings are treated as NULL
	sanity_check(config->shm_name, "Shared memory name cannot be empty");
	sanity_check(config->shm_size > (10 * 2 * 1024 * 1024), "Small shared memory sizes (less than 10 pages) are not supported");
	sanity_check(((uintptr_t)config->shm_start) >= 4096, "Mapping shared memory at page 0 is not allowed");
	sanity_check(config->shm_isolation_level, "Isolation level cannot be empty");
	sanity_check_str(config->shm_isolation_level, "Unknown value for shared memory isolation", "process", "user", "group", "public");

	sanity_check(config->governor_policy, "Governor policy cannot be empty");
	sanity_check_str(config->governor_policy, "Unknown value for governor policy", "hybrid", "busy", "idle");
	if (!strcmp(config->governor_policy, "hybrid") && config->governor_spins == 0)
		nosv_warn("The governor was configured with the \"hybrid\" policy, but the number of spins is zero.\n The governor will behave like an \"idle\" policy.");

	sanity_check(config->cpumanager_binding, "The CPU binding for the CPU manager cannot be empty");

	sanity_check(config->affinity_default, "The default affinity cannot be empty");
	sanity_check(config->affinity_default_policy, "The default affinity policy cannot be empty");
	sanity_check_str(config->affinity_default_policy, "Affinity policy must be one of: strict or preferred", "strict", "preferred");

	sanity_check_str(config->hwcounters_backend, "Currently available hardware counter backends: papi, none", "none", "papi");

	sanity_check_str(config->instrumentation_version, "Currently available instrumentations: ovni, none", "ovni", "none");

	sanity_check(config->thread_stack_size >= PTHREAD_STACK_MIN, "Thread stack size too small");

	return ret;
}

// Support functions to parse the different data types from TOML or strings

#define TOML_PARSE_FN(_name_, _type_, _toml_fn_datum_) \
	static inline int __toml_parse_ ## _name_(_type_ *ptr, toml_datum_t datum); \
	static inline int toml_table_parse_ ## _name_(_type_ *ptr, toml_table_t *table, const char *name) \
	{ \
		toml_datum_t datum = _toml_fn_datum_ ## _in(table, name); \
		return __toml_parse_ ## _name_(ptr, datum); \
	} \
	static inline int toml_array_parse_ ## _name_(_type_ *ptr, toml_array_t *array, int idx) \
	{ \
		toml_datum_t datum = _toml_fn_datum_ ## _at(array, idx); \
		return __toml_parse_ ## _name_(ptr, datum); \
	} \
	static inline int __toml_parse_ ## _name_(_type_ *ptr, toml_datum_t datum)

// Each of these functions has two arguments then, a "ptr" which is a pointer to
// the parsed type (in this case, int64_t *ptr), and then a "datum" which is the obtained
// toml_datum_t. The generated functions are toml_array_parse_int64 and toml_table_parse_int64
TOML_PARSE_FN(int64, int64_t, toml_int)
{
	if (datum.ok) {
		*ptr = datum.u.i;
		return 0;
	}

	return 1;
}

static inline int string_parse_int64(int64_t *ptr, const char *value)
{
	int64_t val;
	errno = 0;
	val = strtoll(value, NULL, 10);
	if (!errno) {
		*ptr = val;
		return 0;
	}

	return 1;
}

TOML_PARSE_FN(ptr, void *, toml_int)
{
	if (datum.ok) {
		*ptr = ((void *)datum.u.i);
		return 0;
	}

	return 1;
}

static inline int string_parse_ptr(void **ptr, const char *value)
{
	void *val;

	// Must start with 0x. If the first char is the null character, the second
	// check will not be executed, which is fine
	if (value[0] == '0' && value[1] == 'x') {
		errno = 0;
		// Base 16
		val = (void *)strtoull(value + 2, NULL, 16);
		if (!errno) {
			*ptr = val;
			return 0;
		}
	}

	return 1;
}

static inline int parse_str_size(size_t *ptr, const char *str)
{
	char *endptr;

	// strtoull may return 0 on success and on error, which means
	// we have to edit errno
	errno = 0;
	size_t result = strtoull(str, &endptr, 10);

	if (errno)
		return 1;

	switch (*endptr) {
		case 'E':
		case 'e':
			result <<= 10;
			fallthrough;
		case 'P':
		case 'p':
			result <<= 10;
			fallthrough;
		case 'T':
		case 't':
			result <<= 10;
			fallthrough;
		case 'G':
		case 'g':
			result <<= 10;
			fallthrough;
		case 'M':
		case 'm':
			result <<= 10;
			fallthrough;
		case 'K':
		case 'k':
			result <<= 10;
			// Parsed a valid character
			goto valid;
		default:
			break;
	}

	// Here we can arrive if there is no character (which is correct), or if there is an invalid character (incorrect)
	if (*endptr != '\0')
		return 1;

valid:
	*ptr = result;
	return 0;
}

TOML_PARSE_FN(size, size_t, toml_string)
{
	if (datum.ok) {
		int ret = parse_str_size(ptr, datum.u.s);
		free(datum.u.s);
		return ret;
	}

	return 1;
}

static inline int string_parse_size(size_t *ptr, const char *value)
{
	return parse_str_size(ptr, value);
}

TOML_PARSE_FN(uint64, uint64_t, toml_int)
{
	if (datum.ok && datum.u.i >= 0) {
		*ptr = ((uint64_t)datum.u.i);
		return 0;
	}

	return 1;
}

static inline int string_parse_uint64(uint64_t *ptr, const char *value)
{
	uint64_t val;
	errno = 0;
	val = strtoull(value, NULL, 10);
	if (!errno) {
		*ptr = val;
		return 0;
	}

	return 1;
}

TOML_PARSE_FN(str, char *, toml_string)
{
	if (datum.ok) {
		// Free the old option
		if (*ptr)
			free(*ptr);

		// Empty strings are codified as NULL
		if (strlen(datum.u.s) == 0) {
			*ptr = NULL;
			free(datum.u.s);
		} else {
			*ptr = datum.u.s;
		}

		return 0;
	}

	return 1;
}

static inline int string_parse_str(char **ptr, const char *value)
{
	// Free the old string
	if (*ptr)
		free(*ptr);

	if (strlen(value) == 0) {
		*ptr = NULL;
	} else {
		*ptr = strdup(value);
		assert(*ptr);
	}


	return 0;
}

TOML_PARSE_FN(bool, int, toml_bool)
{
	if (datum.ok) {
		*ptr = datum.u.b;
		return 0;
	}

	return 1;
}

static inline int string_parse_bool(int *ptr, const char *value)
{
	// We only accept "true" or "false" in lower-case
	if (!strcmp(value, "true")) {
		*ptr = 1;
	} else if (!strcmp(value, "false")) {
		*ptr = 0;
	} else {
		return 1;
	}

	return 0;
}

static inline int toml_table_parse_list_str(string_list_t *ptr, toml_table_t *table, const char *name)
{
	if (ptr->strings) {
		int num_strings = ptr->num_strings;
		for (int i = 0; i < num_strings; ++i) {
			free(ptr->strings[i]);
		}
		free(ptr->strings);
	}

	toml_array_t *array = toml_array_in(table, name);
	int nelems = toml_array_nelem(array);
	ptr->num_strings = nelems;
	ptr->strings = (char **) malloc(sizeof(*(ptr->strings)) * nelems);
	for (int i = 0; i < nelems; ++i) {
		toml_datum_t datum = toml_string_at(array, i);
		if (!datum.ok) return 1;

		(ptr->strings)[i] = strdup(datum.u.s);
		free(datum.u.s);
	}

	return 0;
}

void config_free(void)
{
	size_t nconfig = sizeof(config_spec_list) / sizeof(config_spec_t);

	for (size_t i = 0; i < nconfig; ++i) {
		if (config_spec_list[i].type == TYPE_STR) {
			void **opt = PTR_TO(void *, &nosv_config, config_spec_list[i].member_offset);
			if (*opt)
				free(*opt);
		} else if (config_spec_list[i].type == TYPE_LIST_STR) {
			string_list_t *opt = PTR_TO(string_list_t, &nosv_config, config_spec_list[i].member_offset);
			if (opt->num_strings > 0) {
				for (int i = 0; i < opt->num_strings; ++i) {
					assert(opt->strings[i]);

					free(opt->strings[i]);
				}
				free(opt->strings);
			}
		}
	}
}

// The following rules are followed to find the config file:
//   1. Check for NOSV_CONFIG environment variable for the path. If there is a path but no file is found, stop with an error
//   2. Look for a nosv.toml in the current directory
//   3. Look for a nosv.toml in the installation path
//   4. If there still is no file, stop with an error
static inline void config_find(void)
{
	int cnt;
	const char *env_config_path = getenv("NOSV_CONFIG");

	// Build the default config file path
	cnt = snprintf(default_config_path, MAX_CONFIG_PATH, "%s/nosv.toml", INSTALLED_CONFIG_DIR);
	if (cnt >= MAX_CONFIG_PATH)
		nosv_abort("The installation path for the default nos-v config file is too long");

	// 1. NOSV_CONFIG
	if (env_config_path) {
		// Can we access the file for reading?
		if (access(env_config_path, R_OK)) {
			// We cannot. Lets print the error by stderr and then die
			nosv_abort("Failed to find the file specified in NOSV_CONFIG");
		}

		// Greater or equal strict to account for the null character
		if (strlen(env_config_path) >= MAX_CONFIG_PATH) {
			nosv_abort("Path specified in NOSV_CONFIG is too long");
		}

		strncpy(config_path, env_config_path, MAX_CONFIG_PATH);

		return;
	}

	// 2. Current directory
	if (!getcwd(config_path, MAX_CONFIG_PATH)) {
		nosv_abort("Failed to get current working directory");
	}

	const char *current_path = strdup(config_path);
	assert(current_path);
	cnt = snprintf(config_path, MAX_CONFIG_PATH, "%s/nosv.toml", current_path);
	free((void *)current_path);

	if (cnt >= MAX_CONFIG_PATH) {
		nosv_warn("The current working path is too long, if there is a config file in the current directory it will not be used.");
	} else if (!access(config_path, R_OK)) {
		// Found file in current path
		return;
	}

	// 3. Default config path (installation)
	strncpy(config_path, default_config_path, MAX_CONFIG_PATH);

	if (!access(config_path, R_OK)) {
		return;
	}

	nosv_abort("Failed to find a suitable nOS-V config file\n"
			   "Please, set the config file location through the NOSV_CONFIG environment variable\n"
			   "or place a nosv.toml file in the current working directory.");
}

static inline int config_parse_array_internal(
	config_spec_t *spec, rt_config_t *config, toml_array_t *toml_array, generic_array_t *parsed_array, int dimensions_left)
{
	// This can only happen if we have an extraneous element
	if (!toml_array)
		return 1;

	int ret;

	int nelem = toml_array_nelem(toml_array);
	parsed_array->n = nelem;

	if (dimensions_left == 1) {
		// Parse directly
		parsed_array->items = calloc(nelem, config_spec_type_size[spec->type]);

		for (int i = 0; i < nelem; ++i) {
			switch (spec->type) {
				case TYPE_INT64:
					ret = toml_array_parse_int64(PTR_TO_ELEM(int64_t, parsed_array->items, i), toml_array, i);
					break;
				case TYPE_PTR:
					ret = toml_array_parse_ptr(PTR_TO_ELEM(void *, parsed_array->items, i), toml_array, i);
					break;
				case TYPE_SIZE:
					ret = toml_array_parse_size(PTR_TO_ELEM(size_t, parsed_array->items, i), toml_array, i);
					break;
				case TYPE_UINT64:
					ret = toml_array_parse_uint64(PTR_TO_ELEM(uint64_t, parsed_array->items, i), toml_array, i);
					break;
				case TYPE_STR:
					ret = toml_array_parse_str(PTR_TO_ELEM(char *, parsed_array->items, i), toml_array, i);
					break;
				case TYPE_BOOL:
					ret = toml_array_parse_bool(PTR_TO_ELEM(int, parsed_array->items, i), toml_array, i);
					break;
				default:
					ret = 1;
			}

			if (ret)
				return 1;
		}
	} else {
		// Build array and go one dimension lower
		parsed_array->items = malloc(nelem * sizeof(generic_array_t));
		generic_array_t *subarrays = (generic_array_t *) parsed_array->items;

		for (int i = 0; i < nelem; ++i) {
			ret = config_parse_array_internal(spec, config, toml_array_at(toml_array, i), &subarrays[i], dimensions_left - 1);
			if (ret)
				return 1;
		}
	}

	return 0;
}

static inline int config_parse_array(config_spec_t *spec, rt_config_t *config, toml_table_t *table, const char *element_name)
{
	generic_array_t *parsed_array = PTR_TO(generic_array_t, config, spec->member_offset);

	return config_parse_array_internal(spec, config, toml_array_in(table, element_name), parsed_array, spec->dimensions);
}

static inline int config_parse_single_element(config_spec_t *spec, rt_config_t *config, toml_table_t *table, const char *element_name)
{
	// Try to parse a single TOML element
	// This will be different depending on the type, which we have to be aware of to use the relevant method
	// Note that on earlier versions of the TOML C99 library, we could use toml_raw_t, which is now deprecated

	switch (spec->type) {
		case TYPE_INT64:
			return toml_table_parse_int64(PTR_TO(int64_t, config, spec->member_offset), table, element_name);
		case TYPE_PTR:
			return toml_table_parse_ptr(PTR_TO(void *, config, spec->member_offset), table, element_name);
		case TYPE_SIZE:
			return toml_table_parse_size(PTR_TO(size_t, config, spec->member_offset), table, element_name);
		case TYPE_UINT64:
			return toml_table_parse_uint64(PTR_TO(uint64_t, config, spec->member_offset), table, element_name);
		case TYPE_STR:
			return toml_table_parse_str(PTR_TO(char *, config, spec->member_offset), table, element_name);
		case TYPE_BOOL:
			return toml_table_parse_bool(PTR_TO(int, config, spec->member_offset), table, element_name);
		case TYPE_LIST_STR:
			return toml_table_parse_list_str(PTR_TO(string_list_t, config, spec->member_offset), table, element_name);
		default:
			return 1;
	}
}

static inline int config_parse_single_spec(config_spec_t *spec, rt_config_t *config, toml_table_t *table)
{
	int ret = 0;

	// First, traverse to the relevant toml section
	// We could use strtok for this but we need to know which is the last token
	char *orig_str = strdup(spec->name);
	assert(orig_str);
	char *curr_str = orig_str;
	char *next_token;
	toml_table_t *curr_table = table;

	while ((next_token = strchr(curr_str, CONFIG_SECTION_TOKEN))) {
		// There is a "next_token", hence we have a section name
		// First, do the strtok dance to null-terminate the string
		*next_token = '\0';

		// Now, grab the relevant table
		curr_table = toml_table_in(table, curr_str);

		// Restore the token
		*next_token = CONFIG_SECTION_TOKEN;

		// If the table is NULL, we can return
		if (!curr_table)
			goto empty;

		// Keep iterating
		curr_str = (next_token + 1);
	}

	// We have found the table and now have in curr_str the name of the element.
	assert(curr_table);
	assert(curr_str);
	assert(*curr_str);

	// Check if the key is actually present
	// We have to resort to the old interface for this
	if (!toml_raw_in(curr_table, curr_str) && !toml_array_in(curr_table, curr_str))
		goto empty;

	if (spec->dimensions)
		ret = config_parse_array(spec, config, curr_table, curr_str);
	else
		ret = config_parse_single_element(spec, config, curr_table, curr_str);

	if (ret)
		nosv_warn("Error parsing configuration option %s", orig_str);

empty:
	free(orig_str);
	return ret;
}

static inline int config_populate(rt_config_t *config, toml_table_t *table)
{
	size_t nconfig = sizeof(config_spec_list) / sizeof(config_spec_t);
	int fail = 0;

	for (size_t i = 0; i < nconfig; ++i) {
		config_spec_t *spec = &config_spec_list[i];
		fail += config_parse_single_spec(spec, config, table);
	}

	return fail;
}

static inline void config_print_option(rt_config_t *config, config_spec_t *spec)
{
	fprintf(stderr, "%s = ", spec->name);

	switch (spec->type) {
		case TYPE_INT64:
			fprintf(stderr, "%" PRId64, *PTR_TO(int64_t, config, spec->member_offset));
			break;
		case TYPE_PTR:
			fprintf(stderr, "%p", *PTR_TO(void *, config, spec->member_offset));
			break;
		case TYPE_SIZE:
			fprintf(stderr, "%lu", *PTR_TO(size_t, config, spec->member_offset));
			break;
		case TYPE_UINT64:
			fprintf(stderr, "%" PRIu64, *PTR_TO(uint64_t, config, spec->member_offset));
			break;
		case TYPE_STR:
			fprintf(stderr, "\"%s\"", *PTR_TO(char *, config, spec->member_offset));
			break;
		case TYPE_BOOL:
			fprintf(stderr, "%d", *PTR_TO(int, config, spec->member_offset));
			break;
	}

	fputs("\n", stderr);
}

// For debug purposes
static inline void config_dump(rt_config_t *config)
{
	size_t nconfig = sizeof(config_spec_list) / sizeof(config_spec_t);

	fprintf(stderr, "Using configuration file %s\n"
					"Parsed options: \n",
		config_path);

	for (size_t i = 0; i < nconfig; ++i) {
		config_spec_t *spec = &config_spec_list[i];
		config_print_option(config, spec);
	}
}

static inline int config_parse_single_override(rt_config_t *config, char *option, const char *value)
{
	// Search for a config option spec with the same name
	size_t nconfig = sizeof(config_spec_list) / sizeof(config_spec_t);

	// Trim trailing whitespaces from "option"
	// Relevant: option cannot start with a whitespace
	assert(option);
	assert(*option != ' ');
	int len = strlen(option);

	// Option is empty
	if (!len) {
		nosv_warn("Empty options are invalid on config override");
		return 1;
	}

	int newlen = len;
	while (newlen > 0 && option[newlen - 1] == ' ')
		--newlen;
	option[newlen] = '\0';

	config_spec_t *spec = NULL;
	for (size_t i = 0; i < nconfig; ++i) {
		if (strcmp(config_spec_list[i].name, option) == 0) {
			spec = &config_spec_list[i];
			break;
		}
	}

	// If newlen != len, we had to trim something
	// Let's un-trim it
	if (len > newlen)
		option[newlen] = ' ';

	if (!spec) {
		nosv_warn("Unknown option in config override: %s", option);
		return 1;
	}

	// Trim front whitespaces from value
	while (*value == ' ')
		++value;

	// Parse the found option according to its spec.
	switch (spec->type) {
		case TYPE_INT64:
			return string_parse_int64(PTR_TO(int64_t, config, spec->member_offset), value);
		case TYPE_PTR:
			return string_parse_ptr(PTR_TO(void *, config, spec->member_offset), value);
		case TYPE_SIZE:
			return string_parse_size(PTR_TO(size_t, config, spec->member_offset), value);
		case TYPE_UINT64:
			return string_parse_uint64(PTR_TO(uint64_t, config, spec->member_offset), value);
		case TYPE_STR:
			return string_parse_str(PTR_TO(char *, config, spec->member_offset), value);
		case TYPE_BOOL:
			return string_parse_bool(PTR_TO(int, config, spec->member_offset), value);
		default:
			return 1;
	}
}

static inline int config_parse_override(rt_config_t *config)
{
	int fail = 0;
	const char *config_override = getenv("NOSV_CONFIG_OVERRIDE");

	// There may be no override
	if (config_override == NULL || strlen(config_override) == 0)
		return 0;

	// Dup to prevent changing the environment variable
	char *orig_str = strdup(config_override);
	assert(orig_str);
	char *current_option = strtok(orig_str, ",");

	while (current_option) {
		// First, trim the first characters, to allow for spaces between options
		while (current_option && *current_option == ' ')
			current_option++;

		// Let's extract the name and the value
		char *separator = strchr(current_option, '=');
		if (separator) {
			// Replace separator for null character
			*separator = '\0';

			if (config_parse_single_override(config, current_option, separator + 1)) {
				nosv_warn("Could not parse value \"%s\" for option \"%s\"", separator + 1, current_option);
				fail = 1;
			}

			*separator = '=';
		} else {
			// This may be invalid format, or just empty, but empty variables are allowed
			if (strlen(current_option) > 0) {
				nosv_warn("Invalid format in configuration override: \"%s\"", current_option);
				fail = 1;
			}
		}

		current_option = strtok(NULL, ",");
	}

	free(orig_str);
	return fail;
}

static inline void config_preset_isolated(void)
{
	assert(nosv_config.shm_isolation_level);
	free((void *)nosv_config.shm_isolation_level);
	nosv_config.shm_isolation_level = strdup("process");

	assert(nosv_config.cpumanager_binding);
	free((void *)nosv_config.cpumanager_binding);
	nosv_config.cpumanager_binding = strdup("inherit");

	assert(nosv_config.affinity_default);
	free((void *)nosv_config.affinity_default);
	nosv_config.affinity_default = strdup("all");
}

static inline void config_preset_shared_mpi(void)
{
	assert(nosv_config.shm_isolation_level);
	free((void *)nosv_config.shm_isolation_level);
	nosv_config.shm_isolation_level = strdup("user");

	assert(nosv_config.cpumanager_binding);
	free((void *)nosv_config.cpumanager_binding);
	cpu_get_all_mask(&nosv_config.cpumanager_binding);

	assert(nosv_config.affinity_default_policy);
	free((void *)nosv_config.affinity_default_policy);
	nosv_config.affinity_default_policy = strdup("preferred");

	// Now, see if we can have a best guess at determining a core or NUMA affinity by default
	char *new_affinity;
	int ret = locality_get_default_affinity(&new_affinity);

	if (ret) {
		nosv_warn("Could not determine a valid affinity by default. This can happen if the initial process"
				  " affinity does not constrain to a single core or NUMA node, and therefore a valid nOS-V affinity annotation"
				  " does not exist");
	} else {
		free((void *) nosv_config.affinity_default);
		nosv_config.affinity_default = new_affinity;
	}
}

static inline void config_parse_preset(void)
{
	// For now, the only preset is the environment variable NOSV_PRESET
	// On NOSV_PRESET="isolated", we inherit the CPU mask from the process and set isolation_level to process
	// On NOSV_PRESET="shared-mpi", we use all available CPUs in the system and stablish preferred affinity for all tasks

	const char *preset = getenv("NOSV_PRESET");
	if (!preset || strlen(preset) == 0)
		return;

	if (!strcmp(preset, "isolated"))
		config_preset_isolated();
	else if (!strcmp(preset, "shared-mpi"))
		config_preset_shared_mpi();
	else
		nosv_abort("Unknown value for NOSV_PRESET. Acceptable values are isolated, shared-mpi");
}

// Find and parse the nOS-V config file
void config_parse(void)
{
	char errbuf[200];
	config_init(&nosv_config);
	// This will abort if we can't find the config
	config_find();

	// Open found config file for reading
	FILE *f = fopen(config_path, "r");
	if (f == NULL) {
		nosv_abort("Failed to open config file for reading");
	}

	// Parse the file
	toml_table_t *conf = toml_parse_file(f, errbuf, sizeof(errbuf));
	fclose(f);

	if (conf == NULL) {
		nosv_warn("%s", errbuf);
		nosv_abort("Failed to parse config file");
	}

	int err = config_populate(&nosv_config, conf);

	toml_free(conf);

	if (err)
		nosv_abort("Could not parse config file correctly");

	// Now parse the configuration overrides
	if (config_parse_override(&nosv_config))
		nosv_abort("Could not parse configuration override");

	// Now, if a "preset" has been activated, override the specific options
	config_parse_preset();

	if (nosv_config.debug_dump_config)
		config_dump(&nosv_config);

	if (config_check(&nosv_config))
		nosv_abort("Configuration sanity checks failed");
}
