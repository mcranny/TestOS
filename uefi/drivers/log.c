#include "platform.h"
#include "drivers/serial.h"
#include "drivers/fb.h"
#include "drivers/console.h"

void log_info(const char *msg)
{
    serial_puts(msg);
    serial_puts("\r\n");
}

void log_warn(const char *msg)
{
    serial_puts(msg);
    serial_puts("\r\n");
}

void log_error(const char *msg)
{
    serial_puts(msg);
    serial_puts("\r\n");
}

void log_hex64(const char *prefix, uint64_t value)
{
    serial_puts(prefix);
    serial_hex64(value);
    serial_puts("\r\n");
}

void panic(const char *msg)
{
    irq_disable();
    /* CLI-only panic; no large framebuffer banners. */
    if (fb_ready()) {
        console_puts("\npanic: ");
        console_puts(msg);
        console_puts("\n");
    } else {
        serial_puts("\r\npanic: ");
        serial_puts(msg);
        serial_puts("\r\n");
    }
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
