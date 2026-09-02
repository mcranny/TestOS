#ifndef PAGING_H
#define PAGING_H

#include "types.h"

#define USER_LOAD_ADDR    0x00200000U
#define USER_STACK_BASE   0x00400000U
#define USER_STACK_PAGES  4U

typedef struct address_space
{
    uint32_t *page_directory;
    uint32_t page_directory_phys;
} address_space_t;

void paging_initialize(void);
void paging_enable(void);
int paging_is_enabled(void);
uint32_t paging_get_directory_address(void);
uint32_t paging_get_mapped_bytes(void);
void paging_flush_tlb(void);

/* Identity-map a physical MMIO region (VA == PA) with PCD. Returns phys base. */
void *paging_map_mmio(uintptr_t physical_address, size_t size);

address_space_t *paging_get_kernel_address_space(void);
address_space_t *address_space_create(void);
void address_space_destroy(address_space_t *address_space);
int address_space_is_valid(const address_space_t *address_space);
void address_space_switch(const address_space_t *address_space);
void address_space_map_user_page(
    address_space_t *address_space,
    uint32_t virtual_address,
    uint32_t physical_address
);
void address_space_map_user_pages(
    address_space_t *address_space,
    uint32_t virtual_address,
    uint32_t physical_address,
    uint32_t page_count
);

int address_space_is_user_page(
    const address_space_t *address_space,
    uint32_t virtual_address
);
int address_space_is_user_range(
    const address_space_t *address_space,
    uint32_t virtual_address,
    size_t length
);

#endif
