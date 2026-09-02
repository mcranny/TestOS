#include "ulib.h"

#define SYS_EXIT  1
#define SYS_WRITE 2

static void syscall2(uint32_t number, uint32_t arg1, uint32_t arg2)
{
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(number), "b"(arg1), "c"(arg2)
        : "memory"
    );
}

static void syscall1(uint32_t number, uint32_t arg1)
{
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(number), "b"(arg1)
        : "memory"
    );
}

static uint32_t string_length(const char *string)
{
    uint32_t length = 0;

    while (string[length] != '\0')
    {
        length++;
    }

    return length;
}

void uwrite(const char *string)
{
    uint32_t length = string_length(string);

    syscall2(SYS_WRITE, (uint32_t)string, length);
}

void uprint_int(int32_t value)
{
    char buffer[16];
    int index = 0;
    uint32_t magnitude;
    int negative = 0;

    if (value < 0)
    {
        negative = 1;
        magnitude = (uint32_t)(-(value + 1)) + 1U;
    }
    else
    {
        magnitude = (uint32_t)value;
    }

    if (magnitude == 0)
    {
        uwrite("0");
        return;
    }

    while (magnitude > 0)
    {
        buffer[index++] = (char)('0' + (magnitude % 10U));
        magnitude /= 10U;
    }

    if (negative)
    {
        buffer[index++] = '-';
    }

    while (index > 0)
    {
        char digit[2];

        digit[0] = buffer[--index];
        digit[1] = '\0';
        uwrite(digit);
    }
}

void uexit(int32_t status)
{
    syscall1(SYS_EXIT, (uint32_t)status);
}
