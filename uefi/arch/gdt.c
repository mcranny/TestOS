#include "arch/gdt.h"
#include "arch/tss.h"
#include "platform.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_tss_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct gdt_table {
    struct gdt_entry null;
    struct gdt_entry code;
    struct gdt_entry data;
    struct gdt_entry user_data;
    struct gdt_entry user_code;
    struct gdt_tss_entry tss;
} __attribute__((packed));

static struct gdt_table gdt;
static struct gdt_ptr gdtr;

void gdt_load(const struct gdt_ptr *gdtr, uint16_t code_sel, uint16_t data_sel);

static void gdt_set_code(struct gdt_entry *e)
{
    e->limit_low = 0;
    e->base_low = 0;
    e->base_mid = 0;
    e->access = 0x9A;
    e->granularity = 0x20;
    e->base_high = 0;
}

static void gdt_set_data(struct gdt_entry *e)
{
    e->limit_low = 0;
    e->base_low = 0;
    e->base_mid = 0;
    e->access = 0x92;
    e->granularity = 0x00;
    e->base_high = 0;
}

static void gdt_set_user_code(struct gdt_entry *e)
{
    e->limit_low = 0;
    e->base_low = 0;
    e->base_mid = 0;
    e->access = 0xFA; /* present, DPL3, code, readable */
    e->granularity = 0x20; /* long mode */
    e->base_high = 0;
}

static void gdt_set_user_data(struct gdt_entry *e)
{
    e->limit_low = 0;
    e->base_low = 0;
    e->base_mid = 0;
    e->access = 0xF2; /* present, DPL3, data, writable */
    e->granularity = 0x00;
    e->base_high = 0;
}

static void gdt_set_tss(struct gdt_tss_entry *e, uint64_t base, uint32_t limit)
{
    e->limit_low = (uint16_t)(limit & 0xffff);
    e->base_low = (uint16_t)(base & 0xffff);
    e->base_mid = (uint8_t)((base >> 16) & 0xff);
    e->access = 0x89;
    e->granularity = (uint8_t)((limit >> 16) & 0x0f);
    e->base_high = (uint8_t)((base >> 24) & 0xff);
    e->base_upper = (uint32_t)(base >> 32);
    e->reserved = 0;
}

void gdt_init(void)
{
    struct tss64 *tss = tss_get();
    uint8_t *p = (uint8_t *)&gdt;
    uint64_t i;

    for (i = 0; i < sizeof(gdt); i++) {
        p[i] = 0;
    }

    gdt_set_code(&gdt.code);
    gdt_set_data(&gdt.data);
    gdt_set_user_data(&gdt.user_data);
    gdt_set_user_code(&gdt.user_code);
    gdt_set_tss(&gdt.tss, (uint64_t)(uintptr_t)tss, (uint32_t)(sizeof(struct tss64) - 1));

    gdtr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr.base = (uint64_t)(uintptr_t)&gdt;

    gdt_load(&gdtr, GDT_KERNEL_CODE, GDT_KERNEL_DATA);
    __asm__ volatile("ltr %0" : : "r"((uint16_t)GDT_TSS) : "memory");
}
