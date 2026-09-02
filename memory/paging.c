#include "paging.h"
#include "memory.h"
#include "heap.h"
#include "pmm.h"

#define PAGE_TABLE_ENTRIES 1024U
#define PAGE_TABLE_COUNT   (IDENTITY_MAP_SIZE / (PAGE_TABLE_ENTRIES * PAGE_SIZE))

typedef uint32_t page_table_entry_t;
typedef uint32_t page_directory_entry_t;

static __attribute__((aligned(PAGE_SIZE)))
page_directory_entry_t page_directory[PAGE_TABLE_ENTRIES];

static __attribute__((aligned(PAGE_SIZE)))
page_table_entry_t page_tables[PAGE_TABLE_COUNT][PAGE_TABLE_ENTRIES];

static address_space_t kernel_address_space;
static uint32_t mapped_bytes;
static int paging_enabled;

static void paging_set_entry(page_table_entry_t *entry, uint32_t frame, uint32_t flags)
{
    *entry = (frame & 0xFFFFF000U) | (flags & 0xFFFU);
}

static void paging_map_page_in(
    page_directory_entry_t *directory,
    uint32_t virtual_address,
    uint32_t physical_address,
    uint32_t flags
)
{
    uint32_t directory_index = virtual_address >> 22;
    uint32_t table_index = (virtual_address >> 12) & 0x3FFU;
    page_table_entry_t *page_table;

    if (directory_index >= PAGE_TABLE_COUNT)
    {
        return;
    }

    page_table = (page_table_entry_t *)(directory[directory_index] & 0xFFFFF000U);
    paging_set_entry(
        &page_table[table_index],
        physical_address,
        flags | PAGE_PRESENT
    );
}

void paging_initialize(void)
{
    uint32_t table_index;
    uint32_t page_index;

    mapped_bytes = 0;
    paging_enabled = 0;

    for (table_index = 0; table_index < PAGE_TABLE_ENTRIES; table_index++)
    {
        page_directory[table_index] = 0;
    }

    for (table_index = 0; table_index < PAGE_TABLE_COUNT; table_index++)
    {
        page_directory[table_index] =
            (uint32_t)&page_tables[table_index][0] | PAGE_PRESENT | PAGE_WRITE;

        for (page_index = 0; page_index < PAGE_TABLE_ENTRIES; page_index++)
        {
            uint32_t frame =
                (table_index * PAGE_TABLE_ENTRIES + page_index) * PAGE_SIZE;

            paging_set_entry(
                &page_tables[table_index][page_index],
                frame,
                PAGE_PRESENT | PAGE_WRITE
            );
            mapped_bytes += PAGE_SIZE;
        }
    }

    kernel_address_space.page_directory = page_directory;
    kernel_address_space.page_directory_phys = (uint32_t)page_directory;
}

void paging_enable(void)
{
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(page_directory)
        : "memory"
    );

    {
        uint32_t cr0;

        __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= 0x80000000U;
        __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    }

    paging_enabled = 1;
}

int paging_is_enabled(void)
{
    return paging_enabled;
}

uint32_t paging_get_directory_address(void)
{
    return (uint32_t)page_directory;
}

uint32_t paging_get_mapped_bytes(void)
{
    return mapped_bytes;
}

address_space_t *paging_get_kernel_address_space(void)
{
    return &kernel_address_space;
}

address_space_t *address_space_create(void)
{
    address_space_t *address_space;
    page_directory_entry_t *new_directory;
    uint32_t directory_phys;
    uint32_t table_index;

    address_space = (address_space_t *)kmalloc(sizeof(address_space_t));

    if (address_space == NULL)
    {
        return NULL;
    }

    directory_phys = pmm_alloc_frame();

    if (directory_phys == 0)
    {
        kfree(address_space);
        return NULL;
    }

    new_directory = (page_directory_entry_t *)directory_phys;
    memset(new_directory, 0, PAGE_SIZE);
    address_space->page_directory = new_directory;
    address_space->page_directory_phys = directory_phys;

    for (table_index = 0; table_index < PAGE_TABLE_COUNT; table_index++)
    {
        uint32_t new_table_phys;
        page_table_entry_t *new_table;
        page_table_entry_t *kernel_table;

        if ((page_directory[table_index] & PAGE_PRESENT) == 0)
        {
            continue;
        }

        new_table_phys = pmm_alloc_frame();

        if (new_table_phys == 0)
        {
            address_space_destroy(address_space);
            return NULL;
        }

        new_table = (page_table_entry_t *)new_table_phys;
        kernel_table = (page_table_entry_t *)(page_directory[table_index] & 0xFFFFF000U);
        memcpy(new_table, kernel_table, PAGE_SIZE);

        new_directory[table_index] =
            new_table_phys | (page_directory[table_index] & 0xFFFU);
    }

    return address_space;
}

void address_space_destroy(address_space_t *address_space)
{
    uint32_t table_index;

    if (address_space == NULL || address_space == &kernel_address_space)
    {
        return;
    }

    for (table_index = 0; table_index < PAGE_TABLE_COUNT; table_index++)
    {
        uint32_t entry = address_space->page_directory[table_index];

        if ((entry & PAGE_PRESENT) == 0)
        {
            continue;
        }

        pmm_free_frame(entry & 0xFFFFF000U);
    }

    pmm_free_frame(address_space->page_directory_phys);
    kfree(address_space);
}

int address_space_is_valid(const address_space_t *address_space)
{
    uint32_t address;
    uint32_t mapped_bytes;

    if (address_space == NULL)
    {
        return 0;
    }

    /*
     * Reject wild PCB pointers before any field load. A smashed process_t can
     * hold garbage that is not identity-mapped; dereferencing it would #PF in
     * the exception path and hang the system.
     */
    address = (uint32_t)address_space;

    if ((address & 3U) != 0)
    {
        return 0;
    }

    mapped_bytes = paging_get_mapped_bytes();

    if (mapped_bytes == 0 ||
        address < 0x00100000U ||
        address + sizeof(*address_space) > mapped_bytes)
    {
        return 0;
    }

    if (address_space->page_directory == NULL)
    {
        return 0;
    }

    if (address_space->page_directory_phys == 0)
    {
        return 0;
    }

    if ((address_space->page_directory_phys & 0xFFFU) != 0)
    {
        return 0;
    }

    return 1;
}

void address_space_switch(const address_space_t *address_space)
{
    if (!address_space_is_valid(address_space))
    {
        address_space = paging_get_kernel_address_space();
    }

    if (!address_space_is_valid(address_space))
    {
        return;
    }

    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(address_space->page_directory_phys)
        : "memory"
    );
}

void address_space_map_user_page(
    address_space_t *address_space,
    uint32_t virtual_address,
    uint32_t physical_address
)
{
    uint32_t directory_index;

    if (address_space == NULL)
    {
        return;
    }

    directory_index = virtual_address >> 22;

    if (directory_index >= PAGE_TABLE_COUNT)
    {
        return;
    }

    address_space->page_directory[directory_index] |= PAGE_USER;
    paging_map_page_in(
        address_space->page_directory,
        virtual_address,
        physical_address,
        PAGE_WRITE | PAGE_USER
    );
}

void address_space_map_user_pages(
    address_space_t *address_space,
    uint32_t virtual_address,
    uint32_t physical_address,
    uint32_t page_count
)
{
    uint32_t index;

    for (index = 0; index < page_count; index++)
    {
        address_space_map_user_page(
            address_space,
            virtual_address + (index * PAGE_SIZE),
            physical_address + (index * PAGE_SIZE)
        );
    }
}

void paging_flush_tlb(void)
{
    uint32_t cr3;

    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

int address_space_is_user_page(
    const address_space_t *address_space,
    uint32_t virtual_address
)
{
    uint32_t directory_index = virtual_address >> 22;
    uint32_t table_index = (virtual_address >> 12) & 0x3FFU;
    uint32_t directory_entry;
    uint32_t table_entry;
    const page_table_entry_t *page_table;

    if (address_space == NULL || directory_index >= PAGE_TABLE_COUNT)
    {
        return 0;
    }

    directory_entry = address_space->page_directory[directory_index];

    if ((directory_entry & PAGE_PRESENT) == 0 || (directory_entry & PAGE_USER) == 0)
    {
        return 0;
    }

    page_table = (const page_table_entry_t *)(directory_entry & 0xFFFFF000U);
    table_entry = page_table[table_index];

    if ((table_entry & PAGE_PRESENT) == 0 || (table_entry & PAGE_USER) == 0)
    {
        return 0;
    }

    return 1;
}

int address_space_is_user_range(
    const address_space_t *address_space,
    uint32_t virtual_address,
    size_t length
)
{
    uint32_t end_address;
    uint32_t page_address;

    if (length == 0)
    {
        return 1;
    }

    if (virtual_address == 0)
    {
        return 0;
    }

    end_address = virtual_address + (uint32_t)length - 1U;

    if (end_address < virtual_address)
    {
        return 0;
    }

    page_address = virtual_address & ~(PAGE_SIZE - 1U);

    while (page_address <= (end_address & ~(PAGE_SIZE - 1U)))
    {
        if (!address_space_is_user_page(address_space, page_address))
        {
            return 0;
        }

        page_address += PAGE_SIZE;
    }

    return 1;
}
