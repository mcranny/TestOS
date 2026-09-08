#ifndef PMM_H
#define PMM_H

#include "memory.h"
#include "types.h"

#define HEAP_RESERVED_PAGES 512U

void pmm_initialize(void);
void pmm_mark_used(uint32_t physical_address, uint32_t length);
void pmm_mark_free(uint32_t physical_address, uint32_t length);

uint32_t pmm_alloc_frame(void);
uint32_t pmm_alloc_contiguous(uint32_t page_count);
void pmm_free_frame(uint32_t physical_address);

uint32_t pmm_get_total_frames(void);
uint32_t pmm_get_free_frames(void);
uint32_t pmm_get_bitmap_address(void);
uint32_t pmm_get_bitmap_size(void);
uint32_t pmm_get_heap_start(void);
uint32_t pmm_get_heap_size(void);

#endif
