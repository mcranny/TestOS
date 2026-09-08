#include "ulib.h"

static unsigned long ustrlen(const char *s)
{
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

void uwrite(const char *string)
{
    unsigned long len = ustrlen(string);
    register long rax __asm__("rax") = 2; /* SYS_WRITE */
    register long rdi __asm__("rdi") = (long)string;
    register long rsi __asm__("rsi") = (long)len;
    __asm__ volatile("syscall" : "+r"(rax) : "r"(rdi), "r"(rsi) : "rcx", "r11", "memory");
}

void uexit(int status)
{
    register long rax __asm__("rax") = 1;
    register long rdi __asm__("rdi") = status;
    __asm__ volatile("syscall" : "+r"(rax) : "r"(rdi) : "rcx", "r11", "memory");
    for (;;) {
    }
}
