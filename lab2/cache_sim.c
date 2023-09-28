#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_SIZE 64

typedef enum { dm, fa } cache_map_t;
typedef enum { uc, sc } cache_org_t;
typedef enum { instruction, data } access_t;

typedef struct {
	uint32_t address;
	access_t type;
} mem_access_t;

typedef struct {
	uint64_t accesses;
	uint64_t hits;
	// You can declare additional statistics if
	// you like, however you are now allowed to
	// remove the accesses or hits
} cache_stat_t;

// DECLARE CACHES AND COUNTERS FOR THE STATS HERE

typedef struct {
	union {
		struct {
			bool dm_valid : 1;
			uint32_t dm_tag : 31;
		};
		struct {};
	};
} cacheline_t;

typedef struct {
	cache_map_t map;
	cache_org_t org;
	cache_stat_t stats;
	uint32_t lines_len;
	cacheline_t *lines;
} cache_t;

typedef struct {
	uint32_t cache_size;
	cache_map_t mapping;
	cache_org_t organization;
} cmdargs_t;

cache_t cache_new(cache_map_t map, cache_org_t org, uint32_t size) {
	uint32_t lines_len = size / LINE_SIZE;
	cache_t cache = {
		.map = map,
		.org = org,
		.stats = { 0 },
		.lines_len = lines_len,
		.lines = calloc(lines_len, sizeof(cacheline_t)),
	};
	return cache;
}

void cache_access(cache_t *cache, mem_access_t access) {
	uint32_t lines_beg;
	uint32_t lines_end;
	uint32_t lines_len;

	// Determine lines which are accessible.
	// * DM - Full access to all lines
	// * FA - Half of lines to instructions, other half for data.
	switch (cache->org) {
	case dm:
		lines_beg = 0;
		lines_len = cache->lines_len;
		lines_end = lines_len;
		break;
	case fa:
		lines_len = cache->lines_len >> 1;
		lines_beg = access.type == instruction ? lines_len : 0;
		lines_end = lines_beg + lines_len;
		break;
	}
}

void cache_free(cache_t *cache) { free(cache->lines); }

/* Reads a memory access from the trace file and returns
 * 1) access type (instruction or data access
 * 2) memory address
 */
mem_access_t read_transaction(FILE *ptr_file) {
	char type;
	mem_access_t access;

	if (fscanf(ptr_file, "%c %x\n", &type, &access.address) == 2) {
		if (type != 'I' && type != 'D') {
			printf("Unkown access type\n");
			exit(0);
		}
		access.type = (type == 'I') ? instruction : data;
		return access;
	}

	/* If there are no more entries in the file,
	 * return an address 0 that will terminate the infinite loop in main
	 */
	access.address = 0;
	return access;
}

cmdargs_t parse_args(int argc, char **argv) {
	cmdargs_t args;

	if (argc != 4) { /* argc should be 2 for correct execution */
		printf(
			"Usage: ./cache_sim [cache size: 128-4096] [cache mapping: dm|fa] "
			"[cache organization: uc|sc]\n");
		exit(0);
	}
	/* argv[0] is program name, parameters start with argv[1] */

	/* Set cache size */
	args.cache_size = atoi(argv[1]);

	/* Set Cache Mapping */
	if (strcmp(argv[2], "dm") == 0) {
		args.mapping = dm;
	} else if (strcmp(argv[2], "fa") == 0) {
		args.mapping = fa;
	} else {
		printf("Unknown cache mapping\n");
		exit(0);
	}

	/* Set Cache Organization */
	if (strcmp(argv[3], "uc") == 0) {
		args.organization = uc;
	} else if (strcmp(argv[3], "sc") == 0) {
		args.organization = sc;
	} else {
		printf("Unknown cache organization\n");
		exit(0);
	}

	return args;
}

int main(int argc, char **argv) {

	/* Read command-line parameters and initialize:
	 * cache_size, cache_mapping and cache_org variables
	 */
	/* IMPORTANT: *IF* YOU ADD COMMAND LINE PARAMETERS (you really don't need
	 * to), MAKE SURE TO ADD THEM IN THE END AND CHOOSE SENSIBLE DEFAULTS SUCH
	 * THAT WE CAN RUN THE RESULTING BINARY WITHOUT HAVING TO SUPPLY MORE
	 * PARAMETERS THAN SPECIFIED IN THE UNMODIFIED FILE (cache_size,
	 * cache_mapping and cache_org)
	 */

	cmdargs_t args = parse_args(argc, argv);

	cache_t cache = cache_new(args.mapping, args.organization, args.cache_size);

	/* Open the file mem_trace.txt to read memory accesses */
	FILE *ptr_file;
	ptr_file = fopen("mem_trace.txt", "r");
	if (!ptr_file) {
		printf("Unable to open the trace file\n");
		return 1;
	}

	/* Loop until whole trace file has been read */
	mem_access_t access;
	while (1) {
		access = read_transaction(ptr_file);
		// If no transactions left, break out of loop
		if (access.address == 0) {
			break;
		}
		printf("%d %x\n", access.accesstype, access.address);
		/* Do a cache access */
		// ADD YOUR CODE HERE
	}

	// We cannot change the lines below :shrug:
	cache_stat_t cache_statistics = cache.stats;

	/* Print the statistics */
	// DO NOT CHANGE THE FOLLOWING LINES!
	printf("\nCache Statistics\n");
	printf("-----------------\n\n");
	printf("Accesses: %ld\n", cache_statistics.accesses);
	printf("Hits:		%ld\n", cache_statistics.hits);
	printf("Hit Rate: %.4f\n",
		   (double)cache_statistics.hits / cache_statistics.accesses);
	// DO NOT CHANGE UNTIL HERE
	// You can extend the memory statistic printing if you like!
	//
	/* Close the trace file */
	fclose(ptr_file);
	return 0;
}
