#include "drivers/console.h"
#include "drivers/serial.h"
#include "drivers/fb.h"
#include "drivers/kbd.h"
#include "input/input.h"
#include "usb/xhci.h"
#include "platform.h"
#include "ethernet.h"

void console_init(void)
{
    fb_console_begin();
}

void console_putc(char c)
{
    if (c == '\n') {
        serial_putc('\r');
    }
    serial_putc(c);
    fb_console_putc(c);
}

void console_puts(const char *text)
{
    while (*text) {
        console_putc(*text++);
    }
}

void console_write_hex64(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;
    console_puts("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        console_putc(digits[(value >> shift) & 0xfU]);
    }
}

char console_getchar(void)
{
    for (;;) {
        fb_console_tick(timer_ticks());
        (void)ethernet_poll();
        xhci_poll();
        char c = kbd_getchar();
        if (c != 0) {
            return c;
        }
        c = input_getchar();
        if (c != 0) {
            return c;
        }
        if (serial_has_char()) {
            c = (char)serial_getc();
            if (c == '\r') c = '\n';
            return c;
        }
        /* Busy-poll: serial RX is not IRQ-driven; hlt missed bytes under QEMU TCP serial. */
        __asm__ volatile("pause");
    }
}
