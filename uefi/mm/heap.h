#ifndef TESTOS_UEFI_MM_HEAP_H
#define TESTOS_UEFI_MM_HEAP_H

#include "types.h"

#define HEAP_MIN_BLOCK_SIZE 16U

void heap_initialize(void);
void *kmalloc(size_t size);
void kfree(void *pointer);

uint64_t heap_get_start(void);
uint64_t heap_get_end(void);
uint64_t heap_get_total_bytes(void);
uint64_t heap_get_used_bytes(void);
uint64_t heap_get_free_bytes(void);

#endif
