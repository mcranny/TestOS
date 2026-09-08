#include "pic.h"
#include "port_io.h"

static void io_wait(void)
{
    outb(0x80, 0);
}

void pic_remap(uint8_t master_offset, uint8_t slave_offset)
{
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, master_offset);
    io_wait();
    outb(PIC2_DATA, slave_offset);
    io_wait();

    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8)
    {
        outb(PIC2_COMMAND, PIC_EOI);
    }

    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_unmask_irq(uint8_t irq)
{
    uint16_t port = PIC1_DATA;

    if (irq >= 8)
    {
        port = PIC2_DATA;
        irq -= 8;
    }

    outb(port, inb(port) & ~(1 << irq));
}
