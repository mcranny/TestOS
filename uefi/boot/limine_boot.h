#ifndef TESTOS_UEFI_BOOT_LIMINE_BOOT_H
#define TESTOS_UEFI_BOOT_LIMINE_BOOT_H

#include "limine.h"

struct boot_info {
    uint64_t hhdm_offset;
    uint64_t kernel_phys_base;
    uint64_t kernel_virt_base;
    uint64_t kernel_size;
    struct limine_memmap_response *memmap;
    struct limine_framebuffer *framebuffer;
    struct limine_smp_response *smp;
    void *rsdp;
};

/* Validate Limine responses and fill boot_info. Returns 0 on success. */
int boot_init(struct boot_info *out);

#endif
