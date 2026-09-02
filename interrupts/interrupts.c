#include "interrupts.h"
#include "exceptions.h"
#include "gdt.h"
#include "keyboard.h"
#include "mouse.h"
#include "pic.h"
#include "syscall_abi.h"
#include "timer.h"
#include "types.h"

#define IDT_ENTRIES 256
#define TIMER_VECTOR       (PIC_MASTER_OFFSET + 0)
#define KEYBOARD_VECTOR    (PIC_MASTER_OFFSET + 1)
#define MOUSE_VECTOR       (PIC_SLAVE_OFFSET + 4)

extern void timer_irq_stub(void);
extern void keyboard_irq_stub(void);
extern void mouse_irq_stub(void);
extern void syscall_stub(void);

#define DECLARE_EXCEPTION(num) extern void exception_##num(void)
DECLARE_EXCEPTION(0);
DECLARE_EXCEPTION(1);
DECLARE_EXCEPTION(2);
DECLARE_EXCEPTION(3);
DECLARE_EXCEPTION(4);
DECLARE_EXCEPTION(5);
DECLARE_EXCEPTION(6);
DECLARE_EXCEPTION(7);
DECLARE_EXCEPTION(8);
DECLARE_EXCEPTION(9);
DECLARE_EXCEPTION(10);
DECLARE_EXCEPTION(11);
DECLARE_EXCEPTION(12);
DECLARE_EXCEPTION(13);
DECLARE_EXCEPTION(14);
DECLARE_EXCEPTION(15);
DECLARE_EXCEPTION(16);
DECLARE_EXCEPTION(17);
DECLARE_EXCEPTION(18);
DECLARE_EXCEPTION(19);
DECLARE_EXCEPTION(20);
DECLARE_EXCEPTION(21);
DECLARE_EXCEPTION(22);
DECLARE_EXCEPTION(23);
DECLARE_EXCEPTION(24);
DECLARE_EXCEPTION(25);
DECLARE_EXCEPTION(26);
DECLARE_EXCEPTION(27);
DECLARE_EXCEPTION(28);
DECLARE_EXCEPTION(29);
DECLARE_EXCEPTION(30);
DECLARE_EXCEPTION(31);

struct idt_entry
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idt_pointer;

static void (*exception_stubs[32])(void) =
{
    exception_0, exception_1, exception_2, exception_3,
    exception_4, exception_5, exception_6, exception_7,
    exception_8, exception_9, exception_10, exception_11,
    exception_12, exception_13, exception_14, exception_15,
    exception_16, exception_17, exception_18, exception_19,
    exception_20, exception_21, exception_22, exception_23,
    exception_24, exception_25, exception_26, exception_27,
    exception_28, exception_29, exception_30, exception_31
};

static void idt_load(void)
{
    __asm__ volatile (
        "lidt (%0)"
        :
        : "r" (&idt_pointer)
    );
}

static void idt_set_gate(uint8_t vector, uint32_t handler, uint8_t type_attr)
{
    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].selector = KERNEL_CODE_SEGMENT;
    idt[vector].zero = 0;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_high = (handler >> 16) & 0xFFFF;
}

void interrupts_register_irq(uint8_t irq, void (*stub)(void))
{
    uint8_t vector;

    if (stub == NULL || irq > 15U)
    {
        return;
    }

    if (irq < 8U)
    {
        vector = (uint8_t)(PIC_MASTER_OFFSET + irq);
    }
    else
    {
        vector = (uint8_t)(PIC_SLAVE_OFFSET + (irq - 8U));
    }

    idt_set_gate(vector, (uint32_t)stub, 0x8E);
}

void interrupts_initialize(void)
{
    uint8_t vector;

    pic_remap(PIC_MASTER_OFFSET, PIC_SLAVE_OFFSET);

    for (uint16_t i = 0; i < IDT_ENTRIES; i++)
    {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].type_attr = 0;
        idt[i].offset_high = 0;
    }

    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base = (uint32_t)&idt;

    for (vector = 0; vector < 32; vector++)
    {
        idt_set_gate(vector, (uint32_t)exception_stubs[vector], 0x8E);
    }

    idt_set_gate(TIMER_VECTOR, (uint32_t)timer_irq_stub, 0x8E);
    idt_set_gate(KEYBOARD_VECTOR, (uint32_t)keyboard_irq_stub, 0x8E);
    idt_set_gate(MOUSE_VECTOR, (uint32_t)mouse_irq_stub, 0x8E);
    idt_set_gate(SYSCALL_VECTOR, (uint32_t)syscall_stub, 0xEF);

    idt_load();

    timer_initialize();
    keyboard_initialize();
    mouse_initialize();
}

void interrupts_enable(void)
{
    __asm__ volatile ("sti");
}
