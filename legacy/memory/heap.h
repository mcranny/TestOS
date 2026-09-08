#ifndef HEAP_H
#define HEAP_H

#include "types.h"

#define HEAP_MIN_BLOCK_SIZE 16U

void heap_initialize(void);
void *kmalloc(size_t size);
void kfree(void *pointer);

uint32_t heap_get_start(void);
uint32_t heap_get_end(void);
uint32_t heap_get_total_bytes(void);
uint32_t heap_get_used_bytes(void);
uint32_t heap_get_free_bytes(void);

#endif
