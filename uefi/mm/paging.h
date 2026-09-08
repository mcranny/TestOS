#ifndef TESTOS_UEFI_MM_PAGING_H
#define TESTOS_UEFI_MM_PAGING_H

#include "boot/limine_boot.h"
#include "types.h"

#define USER_LOAD_ADDR   0x0000000000400000ULL
#define USER_STACK_TOP   0x0000000000800000ULL
#define USER_STACK_PAGES 8ULL

typedef struct address_space {
    uint64_t pml4_phys;
} address_space_t;

void paging_init(const struct boot_info *boot);
int map_page(uint64_t virt, uint64_t phys, uint64_t flags);
int unmap_page(uint64_t virt);
void *map_mmio(uint64_t phys, uint64_t size);
void paging_probe(void);

address_space_t *address_space_kernel(void);
address_space_t *address_space_create(void);
void address_space_destroy(address_space_t *as);
void address_space_switch(address_space_t *as);
int map_user_page(address_space_t *as, uint64_t virt, uint64_t phys, uint64_t flags);
uint64_t address_space_virt_to_phys(address_space_t *as, uint64_t virt);
int address_space_is_user_range(address_space_t *as, uint64_t virt, uint64_t size);
void paging_user_probe(void);

#endif
