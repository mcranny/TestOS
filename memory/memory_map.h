#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include "multiboot.h"
#include "types.h"

#define MEMORY_MAP_MAX_REGIONS 32

typedef struct memory_region
{
    uint64_t base;
    uint64_t length;
    uint32_t type;
} memory_region_t;

void memory_map_initialize(uint32_t magic, const multiboot_info_t *info);
uint32_t memory_map_get_region_count(void);
const memory_region_t *memory_map_get_region(uint32_t index);
uint64_t memory_map_get_total_available(void);
uint64_t memory_map_get_highest_address(void);

#endif
