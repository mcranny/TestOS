#include "user/syscall.h"
#include "task/process.h"
#include "arch/gdt.h"
#include "arch/io.h"
#include "drivers/console.h"
#include "mm/paging.h"
#include "platform.h"

extern uint64_t current_syscall_kernel_rsp;
extern void syscall_entry(void);

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
        for (i = 0; i < len; i++) {
            console_putc(buf[i]);
        }
        return (int64_t)len;
    }
    case SYS_YIELD:
        scheduler_yield();
        return 0;
    case SYS_GETPID:
        return (int64_t)process_get_current_pid();
    case SYS_SLEEP:
        process_sleep_ticks(a0);
        return 0;
    default:
        return -1;
    }
}

void syscall_init(void)
{
    uint64_t efer;
    uint64_t star;
    /*
     * STAR layout (Intel SYSRET):
     *   CS = STAR[63:48] + 16, SS = STAR[63:48] + 8 (RPL forced to 3).
     * With GDT user data @0x18 and user code @0x20, STAR[63:48] must be 0x10
     * (kernel data), NOT 0x18 — otherwise SYSRET CS lands on the TSS (0x28).
     */
    star = ((uint64_t)GDT_KERNEL_DATA << 48) | ((uint64_t)GDT_KERNEL_CODE << 32);
    wrmsr(0xC0000081, star); /* STAR */
    wrmsr(0xC0000082, (uint64_t)(uintptr_t)syscall_entry); /* LSTAR */
    /* Clear IF and NT on SYSCALL entry. */
    wrmsr(0xC0000084, 0x200 | 0x4000); /* FMASK */

    efer = rdmsr(0xC0000080);
    efer |= 1ULL; /* SCE */
    wrmsr(0xC0000080, efer);

    console_puts("syscall OK\n");
}
