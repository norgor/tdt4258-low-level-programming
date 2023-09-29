#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
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

// cacheline_t represents a cache line within a cache.
typedef struct {
	union {
		// Struct for use with DM (direct mapping).
		struct {
			bool dm_valid : 1;
			uint32_t dm_tag : 31;
		};
		// Struct for use with FA (fully associative)
		struct {
			bool fa_valid : 1;
			uint32_t fa_tag : 31;
			uint32_t fa_place_time;
		};
	};
} cacheline_t;

// cache_t represents a cache.
typedef struct {
	cache_map_t map;
	cache_org_t org;
	cache_stat_t stats;
	// The total lines in cache (sum of all lines, even in split cache).
	uint32_t lines_len;
	// Array of cache lines. Split in two at midway point when using
	// split cache.
	cacheline_t *lines;
	// Union for mapping type specific values.
	union {
		// DM (direct mapping) values.
		struct {
			// How much to shift the address to the right in order for the
			// tag to be at the least significant bit.
			uint32_t dm_tag_shift;
			// Bitmask for the index bits of the address.
			uint32_t dm_index_mask;
			// How many bits to shift the address to the right in order for
			// the index to be at the least significant bit.
			uint32_t dm_index_shift;
			// Bitmask for the offset bits of the address.
			uint32_t dm_offset_mask;
		};
		// FA (fully associative) values.
		struct {
			// How much to shift the address to the right in order for the
			// tag to be at the least significant bit.
			uint32_t fa_tag_shift;
			// Bitmask for the offset bits of the address.
			uint32_t fa_offset_mask;
			// Cache time which increases by 1 for every access.
			uint32_t fa_time;
		};
	};
} cache_t;

// cmdargs_t is a convenience struct for reading in the command line arguments.
typedef struct {
	uint32_t cache_size;
	cache_map_t mapping;
	cache_org_t organization;
	char *file;
} cmdargs_t;

// cache_new creates a new cache with the specified mapping, organization and
// size.
cache_t cache_new(cache_map_t map, cache_org_t org, uint32_t size) {
	uint32_t lines_len = size / LINE_SIZE;
	// Initialize cache struct.
	cache_t cache = {
		.map = map,
		.org = org,
		.stats = { 0 },
		.lines_len = lines_len,
		.lines = calloc(lines_len, sizeof(cacheline_t)),
	};
	// Organized lines are halved in split cache. One for instructions, and one
	// for data.
	uint32_t org_lines_len =
		cache.org == uc ? cache.lines_len : (cache.lines_len / 2);
	switch (cache.map) {
	case dm:
		// Set up the cache as a DM cache.
		// subtract one gives us mask for powers of two.
		cache.dm_offset_mask = LINE_SIZE - 1;
		cache.dm_index_shift = __builtin_popcount(cache.dm_offset_mask);
		cache.dm_index_mask = (org_lines_len - 1) << cache.dm_index_shift;
		cache.dm_tag_shift =
			cache.dm_index_shift + __builtin_popcount(cache.dm_index_mask);
		break;
	case fa:
		// Set up the cache as a FA cache.
		// subtract one gives us mask for powers of two.
		cache.fa_time = 0;
		cache.fa_offset_mask = LINE_SIZE - 1;
		cache.fa_tag_shift = __builtin_popcount(cache.fa_offset_mask);
		break;
	}

	return cache;
}

// cache_access_dm performs a DM cache access on the provided lines.
void cache_access_dm(cache_t *cache, mem_access_t access, cacheline_t *lines,
					 uint32_t lines_len) {
	uint32_t tag = access.address >> cache->dm_tag_shift;
	uint32_t index =
		(access.address & cache->dm_index_mask) >> cache->dm_index_shift;
	uint32_t offset = access.address & cache->dm_offset_mask;

	// Check for hit, or evict & replace.
	cacheline_t *line = &lines[index];
	if (line->dm_valid && line->dm_tag == tag) {
		// Cache hit!
		cache->stats.hits++;
	} else {
		// Cache miss!
		line->dm_valid = true;
		line->dm_tag = tag;
	}
}

// cache_access_fa performs a FA cache access on the provided lines.
// It uses a FIFO eviction policy.
void cache_access_fa(cache_t *cache, mem_access_t access, cacheline_t *lines,
					 uint32_t lines_len) {
	uint32_t tag = access.address >> cache->fa_tag_shift;
	uint32_t offset = access.address & cache->fa_offset_mask;

	// Search for a valid cache line with the correct tag.
	bool found = false;
	for (uintptr_t i = 0; i < lines_len; i++) {
		if (lines[i].fa_valid && lines[i].fa_tag == tag) {
			found = true;
		}
	}

	if (found) {
		// Cache hit!
		cache->stats.hits++;
	} else {
		// Cache miss!
		// Evist oldest line or the first invalid line (FIFO).
		// Replace that line with the provided line.
		cacheline_t *evict = &lines[0];
		for (int i = 0; i < lines_len; i++) {
			if (!lines[i].fa_valid) {
				evict = &lines[i];
				break;
			}
			if (lines[i].fa_place_time < evict->fa_place_time) {
				evict = &lines[i];
			}
		}

		// Update the line.
		evict->fa_valid = true;
		evict->fa_place_time = cache->fa_time;
		evict->fa_tag = tag;
	}

	cache->fa_time++;
}

// cache_access performs a cache access.
void cache_access(cache_t *cache, mem_access_t access) {
	uint32_t lines_beg;
	uint32_t lines_len;

	// Determine lines which are accessible.
	// * UC - Full access to all lines
	// * SC - Half of lines to instructions, other half for data.
	switch (cache->org) {
	case uc:
		lines_beg = 0;
		lines_len = cache->lines_len;
		break;
	case sc:
		lines_len = cache->lines_len >> 1;
		lines_beg = access.type == instruction ? lines_len : 0;
		break;
	}

	cacheline_t *lines = &cache->lines[lines_beg];

	// Run the respective cache access function.
	switch (cache->map) {
	case fa:
		cache_access_fa(cache, access, lines, lines_len);
		break;
	case dm:
		cache_access_dm(cache, access, lines, lines_len);
		break;
	}

	cache->stats.accesses++;
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
	if (argc < 4 || argc > 5) { /* argc should be 2 for correct execution */
		printf("Usage: ./cache_sim [cache size: 128-4096] [cache mapping: "
			   "dm|fa] [file]"
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

	if (argc >= 5) {
		args.file = argv[4];
	} else {
		args.file = "mem_trace.txt";
	}

	return args;
}

int main(int argc, char **argv) {

	/* Read command-line parameters and initialize:
	 * cache_size, cache_mapping and cache_org variables
	 */
	/* IMPORTANT: *IF* YOU ADD COMMAND LINE PARAMETERS (you really don't need
	 * to), MAKE SURE TO ADD THEM IN THE END AND CHOOSE SENSIBLE DEFAULTS SUCH
<F9>	 * THAT WE CAN RUN THE RESULTING BINARY WITHOUT HAVING TO SUPPLY MORE
	 * PARAMETERS THAN SPECIFIED IN THE UNMODIFIED FILE (cache_size,
	 * cache_mapping and cache_org)
	 */

	cmdargs_t args = parse_args(argc, argv);

	cache_t cache = cache_new(args.mapping, args.organization, args.cache_size);

	/* Open the file mem_trace.txt to read memory accesses */
	FILE *ptr_file;
	ptr_file = fopen(args.file, "r");
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
		printf("%d %x\n", access.type, access.address);
		/* Do a cache access */
		cache_access(&cache, access);
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
