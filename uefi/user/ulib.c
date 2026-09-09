#include "ulib.h"

static unsigned long ustrlen(const char *s)
{
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

static long usyscall3(long nr, long a0, long a1, long a2)
{
    register long rax __asm__("rax") = nr;
    register long rdi __asm__("rdi") = a0;
    register long rsi __asm__("rsi") = a1;
    register long rdx __asm__("rdx") = a2;
    __asm__ volatile("syscall" : "+r"(rax) : "r"(rdi), "r"(rsi), "r"(rdx) : "rcx", "r11", "memory");
    return rax;
}

void uwrite(const char *string)
{
    unsigned long len = ustrlen(string);
    (void)usyscall3(2, (long)string, (long)len, 0);
}

void uexit(int status)
{
    (void)usyscall3(1, status, 0, 0);
    for (;;) {
    }
}

void uyield(void)
{
    (void)usyscall3(6, 0, 0, 0);
}

void usleep(unsigned long ticks)
{
    (void)usyscall3(9, (long)ticks, 0, 0);
}

int usocket(void) { return (int)usyscall3(3, 0, 0, 0); }
int ubind(int sock, unsigned short port) { return (int)usyscall3(4, sock, port, 0); }
int ulisten(int sock, unsigned char backlog) { return (int)usyscall3(5, sock, backlog, 0); }
int uaccept(int sock) { return (int)usyscall3(8, sock, 0, 0); }
int uconnect(int sock, unsigned int ip, unsigned short port) { return (int)usyscall3(10, sock, (long)ip, port); }
int usend(int sock, const void *buf, unsigned short len) { return (int)usyscall3(11, sock, (long)buf, len); }
int urecv(int sock, void *buf, unsigned short len) { return (int)usyscall3(12, sock, (long)buf, len); }
int uclose(int sock) { return (int)usyscall3(13, sock, 0, 0); }
int uresolve(const char *host, unsigned int *ip_out) { return (int)usyscall3(14, (long)host, (long)ip_out, 0); }
int ufswrite(const char *path, const void *buf, unsigned int len) { return (int)usyscall3(15, (long)path, (long)buf, (long)len); }

void uwrite_u32(unsigned int value)
{
    char tmp[11];
    char out[12];
    int n = 0;
    int i;
    if (value == 0) {
        uwrite("0");
        return;
    }
    while (value > 0) {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    for (i = 0; i < n; i++) {
        out[i] = tmp[n - 1 - i];
    }
    out[n] = '\0';
    uwrite(out);
}

void uwrite_ip(unsigned int ip)
{
    uwrite_u32((ip >> 24) & 0xffU);
    uwrite(".");
    uwrite_u32((ip >> 16) & 0xffU);
    uwrite(".");
    uwrite_u32((ip >> 8) & 0xffU);
    uwrite(".");
    uwrite_u32(ip & 0xffU);
}
