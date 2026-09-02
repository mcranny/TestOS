#include "gdt.h"

struct gdt_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

extern void gdt_load(uint32_t gdt_ptr);
extern void gdt_flush_segments(void);

static struct gdt_entry gdt[6];
static struct gdt_ptr gdt_pointer;

static void gdt_set_entry(
    int index,
    uint32_t base,
    uint32_t limit,
    uint8_t access,
    uint8_t granularity
)
{
    gdt[index].base_low = (uint16_t)(base & 0xFFFF);
    gdt[index].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[index].base_high = (uint8_t)((base >> 24) & 0xFF);
    gdt[index].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[index].granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt[index].granularity |= granularity;
    gdt[index].access = access;
}

void gdt_set_tss_entry(int index, uint32_t base, uint32_t limit)
{
    gdt_set_entry(index, base, limit, 0x89, 0x00);
}

void gdt_initialize(void)
{
    gdt_pointer.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_pointer.base = (uint32_t)&gdt;

    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    gdt_load((uint32_t)&gdt_pointer);
    gdt_flush_segments();
}
