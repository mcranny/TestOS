#include "tss.h"
#include "gdt.h"

struct tss_entry
{
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

extern void gdt_set_tss_entry(int index, uint32_t base, uint32_t limit);
extern void tss_load(uint16_t selector);

static struct tss_entry tss;

void tss_initialize(void)
{
    uint32_t index;

    for (index = 0; index < sizeof(tss); index++)
    {
        ((uint8_t *)&tss)[index] = 0;
    }

    tss.ss0 = KERNEL_DATA_SEGMENT;
    tss.iomap_base = sizeof(tss);

    gdt_set_tss_entry(5, (uint32_t)&tss, sizeof(tss) - 1);
    tss_load(TSS_SEGMENT);
}

void tss_set_kernel_stack(uint32_t stack_top)
{
    tss.esp0 = stack_top;
}
