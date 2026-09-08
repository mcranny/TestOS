#ifndef TESTOS_UEFI_MM_PMM_H
#define TESTOS_UEFI_MM_PMM_H

#include "boot/limine_boot.h"

void pmm_init(const struct boot_info *boot);
uint64_t pmm_alloc_frame(void);
uint64_t pmm_alloc_contiguous(uint64_t frames);
void pmm_free_frame(uint64_t phys);
uint64_t pmm_total_frames(void);
uint64_t pmm_free_frames(void);

#endif
