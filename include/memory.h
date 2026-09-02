#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

#define PAGE_SIZE          4096U
#define PAGE_SHIFT         12
#define KERNEL_LOAD_ADDR   0x00100000U
#define IDENTITY_MAP_SIZE  (128U * 1024U * 1024U)
#define PMM_MAX_ADDRESS    IDENTITY_MAP_SIZE

#define PAGE_PRESENT       (1U << 0)
#define PAGE_WRITE         (1U << 1)
#define PAGE_USER          (1U << 2)

#define ALIGN_UP(value, alignment) \
    (((value) + ((alignment) - 1U)) & ~((alignment) - 1U))

void *memset(void *destination, int value, size_t count);
void *memcpy(void *destination, const void *source, size_t count);

#endif
