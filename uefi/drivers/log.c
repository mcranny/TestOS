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

static void append_str(char *buf, size_t cap, size_t *len, const char *s)
{
    if (s == NULL || *len >= cap) {
        return;
    }
    while (*s != '\0' && *len + 1U < cap) {
        buf[(*len)++] = *s++;
    }
    buf[*len] = '\0';
}

static void uint_to_dec(uint32_t value, char *buffer, size_t cap)
{
    char temp[11];
    int index = 0;
    size_t out = 0;

    if (cap == 0) {
        return;
    }
    if (value == 0) {
        buffer[0] = '0';
        if (cap > 1U) {
            buffer[1] = '\0';
        }
        return;
    }

    while (value > 0 && index < 11) {
        temp[index++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (index > 0 && out + 1U < cap) {
        buffer[out++] = temp[--index];
    }
    buffer[out] = '\0';
}

static void klog_emit(klog_level_t level, const char *line)
{
    switch (level) {
        case KLOG_WARN:
            log_warn(line);
            break;
        case KLOG_ERROR:
            log_error(line);
            break;
        case KLOG_PANIC:
            panic(line);
            break;
        case KLOG_DEBUG:
        case KLOG_INFO:
        default:
            log_info(line);
            break;
    }
}

void klog(klog_level_t level, const char *category, const char *message)
{
    char line[160];
    size_t len = 0;

    if (level == KLOG_DEBUG && !DEBUG_NET) {
        return;
    }

    line[0] = '\0';
    if (category != NULL) {
        append_str(line, sizeof(line), &len, category);
        append_str(line, sizeof(line), &len, ": ");
    }
    if (message != NULL) {
        append_str(line, sizeof(line), &len, message);
    }
    klog_emit(level, line);
}

void klog_uint(klog_level_t level, const char *category, const char *prefix, uint32_t value)
{
    char line[160];
    char number[12];
    size_t len = 0;

    if (level == KLOG_DEBUG && !DEBUG_NET) {
        return;
    }

    line[0] = '\0';
    if (category != NULL) {
        append_str(line, sizeof(line), &len, category);
        append_str(line, sizeof(line), &len, ": ");
    }
    if (prefix != NULL) {
        append_str(line, sizeof(line), &len, prefix);
    }
    uint_to_dec(value, number, sizeof(number));
    append_str(line, sizeof(line), &len, number);
    klog_emit(level, line);
}
