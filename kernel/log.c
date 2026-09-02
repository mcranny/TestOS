#include "log.h"
#include "serial.h"
#include "terminal.h"

static volatile int kernel_panicked;

static int category_debug_enabled(const char *category)
{
    if (category == NULL)
    {
        return 0;
    }

    if (category[0] == 'M' && category[1] == 'E' && category[2] == 'M' && category[3] == '\0')
    {
        return DEBUG_MEM;
    }
    if (category[0] == 'P' && category[1] == 'R' && category[2] == 'O' && category[3] == 'C' && category[4] == '\0')
    {
        return DEBUG_PROC;
    }
    if (category[0] == 'S' && category[1] == 'C' && category[2] == 'H' && category[3] == 'E' && category[4] == 'D' && category[5] == '\0')
    {
        return DEBUG_SCHED;
    }
    if (category[0] == 'F' && category[1] == 'S' && category[2] == '\0')
    {
        return DEBUG_FS;
    }
    if (category[0] == 'A' && category[1] == 'T' && category[2] == 'A' && category[3] == '\0')
    {
        return DEBUG_ATA;
    }
    if (category[0] == 'I' && category[1] == 'R' && category[2] == 'Q' && category[3] == '\0')
    {
        return DEBUG_IRQ;
    }
    if (category[0] == 'S' && category[1] == 'Y' && category[2] == 'S' && category[3] == 'C' && category[4] == 'A' && category[5] == 'L' && category[6] == 'L' && category[7] == '\0')
    {
        return DEBUG_SYSCALL;
    }
    if (category[0] == 'B' && category[1] == 'O' && category[2] == 'O' && category[3] == 'T' && category[4] == '\0')
    {
        return DEBUG_BOOT;
    }
    if (category[0] == 'P' && category[1] == 'C' && category[2] == 'I' && category[3] == '\0')
    {
        return DEBUG_PCI;
    }
    if (category[0] == 'M' && category[1] == 'O' && category[2] == 'U' && category[3] == 'S' &&
        category[4] == 'E' && category[5] == '\0')
    {
        return DEBUG_MOUSE;
    }

    return 1;
}

static const char *level_name(klog_level_t level)
{
    switch (level)
    {
        case KLOG_DEBUG:
            return "DEBUG";
        case KLOG_INFO:
            return "INFO";
        case KLOG_WARN:
            return "WARN";
        case KLOG_ERROR:
            return "ERROR";
        case KLOG_PANIC:
            return "PANIC";
        default:
            return "LOG";
    }
}

static void emit(const char *text)
{
    if (text == NULL)
    {
        return;
    }

    terminal_write(text);
    serial_write(text);
}

static void uint_to_dec(uint32_t value, char *buffer)
{
    char temp[11];
    int index = 0;
    int out = 0;

    if (value == 0)
    {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }

    while (value > 0)
    {
        temp[index++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (index > 0)
    {
        buffer[out++] = temp[--index];
    }

    buffer[out] = '\0';
}

void klog(klog_level_t level, const char *category, const char *message)
{
    if (level == KLOG_DEBUG && !category_debug_enabled(category))
    {
        return;
    }

    emit("[");
    emit(level_name(level));
    emit("] ");

    if (category != NULL)
    {
        emit(category);
        emit(": ");
    }

    if (message != NULL)
    {
        emit(message);
    }

    emit("\n");
}

void klog_uint(klog_level_t level, const char *category, const char *prefix, uint32_t value)
{
    char number[12];

    if (level == KLOG_DEBUG && !category_debug_enabled(category))
    {
        return;
    }

    emit("[");
    emit(level_name(level));
    emit("] ");

    if (category != NULL)
    {
        emit(category);
        emit(": ");
    }

    if (prefix != NULL)
    {
        emit(prefix);
    }

    uint_to_dec(value, number);
    emit(number);
    emit("\n");
}

int kernel_is_panicked(void)
{
    return kernel_panicked;
}

void panic(const char *subsystem, const char *message)
{
    kernel_panicked = 1;

    __asm__ volatile ("cli");

    emit("\n");
    emit("========================\n");
    emit("       KERNEL PANIC\n");
    emit("========================\n");
    emit("\n");
    emit("Subsystem: ");
    emit(subsystem != NULL ? subsystem : "(unknown)");
    emit("\n");
    emit("Message: ");
    emit(message != NULL ? message : "(none)");
    emit("\n");

    for (;;)
    {
        __asm__ volatile ("hlt");
    }
}
