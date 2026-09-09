#ifndef TESTOS_UEFI_MM_DMA_H
#define TESTOS_UEFI_MM_DMA_H

#include "types.h"

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
