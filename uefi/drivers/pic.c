#include "drivers/pic.h"
#include "arch/io.h"

void pic_remap(uint8_t offset1, uint8_t offset2)
{
    uint8_t mask1 = inb(0x21);
    uint8_t mask2 = inb(0xa1);

    outb(0x20, 0x11);
    outb(0xa0, 0x11);
    outb(0x21, offset1);
    outb(0xa1, offset2);
    outb(0x21, 0x04);
    outb(0xa1, 0x02);
    outb(0x21, 0x01);
    outb(0xa1, 0x01);

    outb(0x21, mask1);
    outb(0xa1, mask2);
}

void pic_mask_all(void)
{
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
}

void pic_unmask(uint8_t irq)
{
    uint16_t port = (irq < 8) ? 0x21 : 0xa1;
    uint8_t value;
    if (irq >= 16) return;
    if (irq >= 8) irq = (uint8_t)(irq - 8);
    value = inb(port) & (uint8_t)~(1U << irq);
    outb(port, value);
}

void pic_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(0xa0, 0x20);
    }
    outb(0x20, 0x20);
}
