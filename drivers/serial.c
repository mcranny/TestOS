#include "serial.h"
#include "port_io.h"
#include "types.h"

#define COM1_PORT 0x3F8

#define COM1_DATA        (COM1_PORT + 0)
#define COM1_IER         (COM1_PORT + 1)
#define COM1_FIFO        (COM1_PORT + 2)
#define COM1_LCR         (COM1_PORT + 3)
#define COM1_MCR         (COM1_PORT + 4)
#define COM1_LSR         (COM1_PORT + 5)

#define LSR_TRANSMIT_EMPTY 0x20

static int serial_ready;

void serial_initialize(void)
{
    outb(COM1_IER, 0x00);    /* Disable interrupts */
    outb(COM1_LCR, 0x80);    /* Enable DLAB */
    outb(COM1_DATA, 0x01);   /* Divisor low: 115200 baud */
    outb(COM1_IER, 0x00);    /* Divisor high */
    outb(COM1_LCR, 0x03);    /* 8N1, DLAB off */
    outb(COM1_FIFO, 0xC7);   /* Enable FIFO, clear, 14-byte threshold */
    outb(COM1_MCR, 0x0B);    /* IRQs enabled, RTS/DSR set */
    serial_ready = 1;
}

void serial_write_char(char c)
{
    if (!serial_ready)
    {
        return;
    }

    if (c == '\n')
    {
        serial_write_char('\r');
    }

    while ((inb(COM1_LSR) & LSR_TRANSMIT_EMPTY) == 0)
    {
    }

    outb(COM1_DATA, (uint8_t)c);
}

void serial_write(const char *str)
{
    if (str == NULL)
    {
        return;
    }

    while (*str != '\0')
    {
        serial_write_char(*str);
        str++;
    }
}
