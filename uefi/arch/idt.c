#include "arch/idt.h"
#include "arch/gdt.h"
#include "platform.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtr;

void idt_set_gate(uint8_t vector, void *handler, uint8_t ist, uint8_t type_attr)
{
    uint64_t addr = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)(addr & 0xffff);
    idt[vector].selector = GDT_KERNEL_CODE;
    idt[vector].ist = ist & 0x7;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_mid = (uint16_t)((addr >> 16) & 0xffff);
    idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xffffffff);
    idt[vector].zero = 0;
}

void idt_init(void)
{
    uint64_t i;
    uint8_t *p = (uint8_t *)idt;
    for (i = 0; i < sizeof(idt); i++) {
        p[i] = 0;
    }
    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base = (uint64_t)(uintptr_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}
