#include "user/syscall.h"
#include "task/process.h"
#include "arch/gdt.h"
#include "arch/io.h"
#include "arch/smap.h"
#include "drivers/console.h"
#include "mm/paging.h"
#include "platform.h"
#include "socket.h"
#include "dns.h"
#include "ipv4.h"
#include "ethernet.h"
#include "fs/tfs.h"
#include "fs/fs.h"

extern uint64_t current_syscall_kernel_rsp;
extern void syscall_entry(void);

static uint16_t ephemeral_port = 41000U;

static void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

static uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void syscall_set_kernel_rsp(uint64_t rsp)
{
    current_syscall_kernel_rsp = rsp;
}

static int copy_user_string(process_t *p, const char *ubuf, char *kbuf, uint64_t cap)
{
    uint64_t i;
    if (!p || !p->as || ubuf == NULL || kbuf == NULL || cap == 0) {
        return 0;
    }
    user_access_begin();
    for (i = 0; i + 1U < cap; i++) {
        if (!address_space_is_user_range(p->as, (uint64_t)(uintptr_t)(ubuf + i), 1)) {
            user_access_end();
            return 0;
        }
        kbuf[i] = ubuf[i];
        if (ubuf[i] == '\0') {
            user_access_end();
            return 1;
        }
    }
    user_access_end();
    kbuf[cap - 1U] = '\0';
    return 1;
}

int64_t syscall_dispatch(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2)
{
    process_t *p = process_get_current();

    switch (nr) {
    case SYS_EXIT:
        process_exit((int32_t)a0);
        return 0;
    case SYS_WRITE: {
        const char *buf = (const char *)(uintptr_t)a0;
        uint64_t len = a1;
        uint64_t i;
        if (!p || !p->as) return -1;
        if (len > 4096) len = 4096;
        if (!address_space_is_user_range(p->as, (uint64_t)(uintptr_t)buf, len)) {
            return -1;
        }
        user_access_begin();
        for (i = 0; i < len; i++) {
            console_putc(buf[i]);
        }
        user_access_end();
        return (int64_t)len;
    }
    case SYS_YIELD:
        (void)ethernet_poll();
        scheduler_yield();
        return 0;
    case SYS_GETPID:
        return (int64_t)process_get_current_pid();
    case SYS_SLEEP:
        process_sleep_ticks(a0);
        return 0;
    case SYS_SOCKET:
        return (int64_t)socket_create();
    case SYS_BIND:
        return (int64_t)socket_bind((int)a0, (uint16_t)a1);
    case SYS_LISTEN:
        return (int64_t)socket_listen((int)a0, (uint8_t)a1);
    case SYS_ACCEPT: {
        int h = socket_accept((int)a0);
        (void)ethernet_poll();
        return (int64_t)h;
    }
    case SYS_CONNECT: {
        uint16_t local = ephemeral_port++;
        if (ephemeral_port < 41000U) {
            ephemeral_port = 41000U;
        }
        (void)ethernet_poll();
        return (int64_t)socket_connect((int)a0, (ipv4_addr_t)a1, (uint16_t)a2, local);
    }
    case SYS_SEND: {
        const void *buf = (const void *)(uintptr_t)a1;
        uint64_t len = a2;
        int64_t n;
        if (!p || !p->as) return -1;
        if (len > 2048) len = 2048;
        if (len > 0 && !address_space_is_user_range(p->as, (uint64_t)(uintptr_t)buf, len)) {
            return -1;
        }
        (void)ethernet_poll();
        user_access_begin();
        n = (int64_t)socket_send((int)a0, buf, (uint16_t)len);
        user_access_end();
        return n;
    }
    case SYS_RECV: {
        void *buf = (void *)(uintptr_t)a1;
        uint64_t len = a2;
        int64_t n;
        if (!p || !p->as) return -1;
        if (len > 2048) len = 2048;
        if (len > 0 && !address_space_is_user_range(p->as, (uint64_t)(uintptr_t)buf, len)) {
            return -1;
        }
        (void)ethernet_poll();
        user_access_begin();
        n = (int64_t)socket_recv((int)a0, buf, (uint16_t)len);
        user_access_end();
        return n;
    }
    case SYS_CLOSE:
        return (int64_t)socket_close((int)a0);
    case SYS_RESOLVE: {
        char host[96];
        ipv4_addr_t ip = 0;
        ipv4_addr_t *out = (ipv4_addr_t *)(uintptr_t)a1;
        if (!p || !p->as) return -1;
        if (!copy_user_string(p, (const char *)(uintptr_t)a0, host, sizeof(host))) {
            return -1;
        }
        if (!address_space_is_user_range(p->as, (uint64_t)(uintptr_t)out, sizeof(ipv4_addr_t))) {
            return -1;
        }
        if (!dns_resolve(host, &ip)) {
            return -1;
        }
        user_access_begin();
        *out = ip;
        user_access_end();
        return 0;
    }
    case SYS_FSWRITE: {
        char path[FS_MAX_PATH];
        const void *buf = (const void *)(uintptr_t)a1;
        uint64_t len = a2;
        int ok;
        if (!p || !p->as) return -1;
        if (!copy_user_string(p, (const char *)(uintptr_t)a0, path, sizeof(path))) {
            return -1;
        }
        if (len > FS_MAX_FILE_SIZE) len = FS_MAX_FILE_SIZE;
        if (len > 0 && !address_space_is_user_range(p->as, (uint64_t)(uintptr_t)buf, len)) {
            return -1;
        }
        user_access_begin();
        ok = tfs_write(path, buf, (uint32_t)len, 0);
        user_access_end();
        if (!ok) {
            return -1;
        }
        return (int64_t)len;
    }
    default:
        return -1;
    }
}

void syscall_init(void)
{
    uint64_t efer;
    uint64_t star;
    star = ((uint64_t)GDT_KERNEL_DATA << 48) | ((uint64_t)GDT_KERNEL_CODE << 32);
    wrmsr(0xC0000081, star);
    wrmsr(0xC0000082, (uint64_t)(uintptr_t)syscall_entry);
    wrmsr(0xC0000084, 0x200 | 0x4000);

    efer = rdmsr(0xC0000080);
    efer |= 1ULL;
    wrmsr(0xC0000080, efer);

    console_puts("syscall OK\n");
}
