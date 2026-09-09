#ifndef TESTOS_UEFI_MM_DMA_H
#define TESTOS_UEFI_MM_DMA_H

#include "types.h"

/*
 * DMA policy: all dma_alloc_* buffers are constrained to physical addresses
 * below 4 GiB so 32-bit-address bus masters (AHCI/NVMe/e1000/xHCI) stay safe.
 * General pmm_alloc_* may return frames above 4 GiB.
 */
#define DMA_ZONE_MAX_PHYS (4ULL * 1024ULL * 1024ULL * 1024ULL)

typedef struct dma_buffer
{
    uint64_t phys;
    void *virt;
    uint64_t frames;
} dma_buffer_t;

int dma_alloc_page(uint64_t *phys_out, void **virt_out);
int dma_alloc_pages(uint64_t frames, dma_buffer_t *out);
void dma_free_pages(dma_buffer_t *buf);

#endif
