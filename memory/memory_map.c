#include "memory_map.h"
#include "memory.h"
#include "port_io.h"

#define FALLBACK_MEMORY_BYTES (128ULL * 1024ULL * 1024ULL)

#define FW_CFG_SEL   0x0510
#define FW_CFG_DATA  0x0511
#define FW_CFG_FILE_DIR 0x0019
#define FW_CFG_MAX_FILES 64U
#define E820_TABLE_MAX_BYTES 2048U

typedef struct fw_cfg_file
{
    uint32_t size;
    uint16_t select;
    uint16_t reserved;
    char name[56];
} __attribute__((packed)) fw_cfg_file_t;

typedef struct e820_entry
{
    uint64_t address;
    uint64_t length;
    uint32_t type;
} __attribute__((packed)) e820_entry_t;

static memory_region_t regions[MEMORY_MAP_MAX_REGIONS];
static uint32_t region_count;
static uint64_t total_available;
static uint64_t highest_address;

static int name_is(const char *name, const char *expected)
{
    while (*expected != '\0')
    {
        if (*name != *expected)
        {
            return 0;
        }

        name++;
        expected++;
    }

    return *name == '\0';
}

static void memory_map_add_region(uint64_t base, uint64_t length, uint32_t type)
{
    if (region_count >= MEMORY_MAP_MAX_REGIONS || length == 0)
    {
        return;
    }

    regions[region_count].base = base;
    regions[region_count].length = length;
    regions[region_count].type = type;
    region_count++;

    if (type == MULTIBOOT_MEMORY_AVAILABLE)
    {
        total_available += length;
    }

    if (base + length > highest_address && base < PMM_MAX_ADDRESS)
    {
        uint64_t capped_end = base + length;

        if (capped_end > PMM_MAX_ADDRESS)
        {
            capped_end = PMM_MAX_ADDRESS;
        }

        if (capped_end > highest_address)
        {
            highest_address = capped_end;
        }
    }
}

static void memory_map_add_fallback(void)
{
    memory_map_add_region(
        KERNEL_LOAD_ADDR,
        FALLBACK_MEMORY_BYTES,
        MULTIBOOT_MEMORY_AVAILABLE
    );
}

static uint32_t fw_cfg_read32(void)
{
    uint32_t value = 0;
    uint32_t shift;

    for (shift = 0; shift < 32; shift += 8)
    {
        value |= (uint32_t)inb(FW_CFG_DATA) << shift;
    }

    return value;
}

static int memory_map_load_multiboot_mmap(const multiboot_info_t *info)
{
    uint32_t offset = 0;

    while (offset < info->mmap_length)
    {
        const multiboot_mmap_entry_t *entry =
            (const multiboot_mmap_entry_t *)(info->mmap_addr + offset);

        memory_map_add_region(entry->addr, entry->len, entry->type);

        offset += entry->size + sizeof(entry->size);
    }

    return region_count > 0;
}

static int memory_map_load_multiboot_basic(const multiboot_info_t *info)
{
    if (!(info->flags & MULTIBOOT_INFO_MEMORY) || info->mem_upper == 0)
    {
        return 0;
    }

    memory_map_add_region(
        0x00100000ULL,
        (uint64_t)info->mem_upper * 1024ULL,
        MULTIBOOT_MEMORY_AVAILABLE
    );

    return region_count > 0;
}

static int memory_map_load_fw_cfg_e820(void)
{
    uint32_t file_count;
    uint32_t file_index;
    uint16_t e820_select = 0;
    uint32_t e820_size = 0;
    uint32_t offset;

    outw(FW_CFG_SEL, FW_CFG_FILE_DIR);
    file_count = fw_cfg_read32();

    if (file_count > FW_CFG_MAX_FILES)
    {
        file_count = FW_CFG_MAX_FILES;
    }

    for (file_index = 0; file_index < file_count; file_index++)
    {
        fw_cfg_file_t file;
        uint8_t *bytes = (uint8_t *)&file;
        uint32_t byte_index;

        for (byte_index = 0; byte_index < sizeof(fw_cfg_file_t); byte_index++)
        {
            bytes[byte_index] = inb(FW_CFG_DATA);
        }

        if (name_is(file.name, "etc/e820"))
        {
            e820_select = file.select;
            e820_size = file.size;
            break;
        }
    }

    if (e820_size == 0)
    {
        return 0;
    }

    if (e820_size > E820_TABLE_MAX_BYTES)
    {
        e820_size = E820_TABLE_MAX_BYTES;
    }

    outw(FW_CFG_SEL, e820_select);

    for (offset = 0; offset + sizeof(e820_entry_t) <= e820_size; offset += sizeof(e820_entry_t))
    {
        e820_entry_t entry;
        uint8_t *bytes = (uint8_t *)&entry;
        uint32_t byte_index;

        for (byte_index = 0; byte_index < sizeof(e820_entry_t); byte_index++)
        {
            bytes[byte_index] = inb(FW_CFG_DATA);
        }

        if (entry.address >= PMM_MAX_ADDRESS)
        {
            continue;
        }

        memory_map_add_region(entry.address, entry.length, entry.type);
    }

    return region_count > 0;
}

void memory_map_initialize(uint32_t magic, const multiboot_info_t *info)
{
    region_count = 0;
    total_available = 0;
    highest_address = 0;

    if (magic == MULTIBOOT_BOOTLOADER_MAGIC && info != NULL)
    {
        if ((info->flags & MULTIBOOT_INFO_MEM_MAP) && info->mmap_addr != 0)
        {
            if (memory_map_load_multiboot_mmap(info))
            {
                return;
            }

            region_count = 0;
            total_available = 0;
            highest_address = 0;
        }

        if (memory_map_load_multiboot_basic(info))
        {
            return;
        }

        region_count = 0;
        total_available = 0;
        highest_address = 0;
    }

    if (memory_map_load_fw_cfg_e820())
    {
        return;
    }

    region_count = 0;
    total_available = 0;
    highest_address = 0;
    memory_map_add_fallback();
}

uint32_t memory_map_get_region_count(void)
{
    return region_count;
}

const memory_region_t *memory_map_get_region(uint32_t index)
{
    if (index >= region_count)
    {
        return NULL;
    }

    return &regions[index];
}

uint64_t memory_map_get_total_available(void)
{
    return total_available;
}

uint64_t memory_map_get_highest_address(void)
{
    return highest_address;
}
