#include "mm/dma.h"
#include "platform.h"
#include "lib/string.h"

int dma_alloc_page(uint64_t *phys_out, void **virt_out)
{
    uint64_t phys;

    if (phys_out == NULL || virt_out == NULL) {
        return 0;
    }

    phys = pmm_alloc_frame();
    if (phys == 0) {
        return 0;
    }

    *phys_out = phys;
    *virt_out = phys_to_virt(phys);
    return 1;
}

int dma_alloc_pages(uint64_t frames, dma_buffer_t *out)
{
    uint64_t phys;

    if (out == NULL || frames == 0) {
        return 0;
    }

    if (frames == 1) {
        if (!dma_alloc_page(&phys, &out->virt)) {
            return 0;
        }
        out->phys = phys;
        out->frames = 1;
        return 1;
    }

    phys = pmm_alloc_contiguous(frames);
    if (phys == 0) {
        return 0;
    }

    out->phys = phys;
    out->virt = phys_to_virt(phys);
    out->frames = frames;
    memset(out->virt, 0, (size_t)(frames * 4096ULL));
    return 1;
}

void dma_free_pages(dma_buffer_t *buf)
{
    uint64_t index;

    if (buf == NULL || buf->phys == 0 || buf->frames == 0) {
        return;
    }

    for (index = 0; index < buf->frames; index++) {
        pmm_free_frame(buf->phys + (index * 4096ULL));
    }

    buf->phys = 0;
    buf->virt = NULL;
    buf->frames = 0;
}
