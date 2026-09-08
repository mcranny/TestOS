#include "drivers/serial.h"
#include "arch/io.h"

void serial_putc(char c)
{
    uint16_t port = 0x3f8;
    uint32_t spin = 1000000;
    while ((inb(port + 5) & 0x20) == 0 && spin-- != 0) { }
    outb(port, (uint8_t)c);
}

void serial_init(void)
{
    outb(0x3f9, 0);      /* disable UART interrupts */
    outb(0x3fb, 0x80);   /* DLAB on */
    outb(0x3f8, 3);      /* 38400 baud divisor */
    outb(0x3f9, 0);
    outb(0x3fb, 3);      /* 8N1 */
    outb(0x3fa, 0);      /* disable FIFO — avoid RX drops while polling */
    outb(0x3fc, 0x0b);   /* DTR | RTS | OUT2 */
}

void serial_puts(const char *text)
{
    while (*text != '\0') {
        serial_putc(*text++);
    }
}

void serial_hex64(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;
    serial_puts("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        serial_putc(digits[(value >> shift) & 0xfU]);
    }
}

int serial_has_char(void)
{
    return (inb(0x3f8 + 5) & 0x01) != 0;
}

char serial_getc(void)
{
    while (!serial_has_char()) { }
    return (char)inb(0x3f8);
}
