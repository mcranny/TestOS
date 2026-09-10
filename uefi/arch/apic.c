#include "arch/apic.h"
#include "arch/io.h"
#include "mm/paging.h"
#include "platform.h"

#define IA32_APIC_BASE_MSR 0x1BU
#define APIC_BASE_ENABLE   (1ULL << 11)
#define APIC_BASE_BSP      (1ULL << 8)

#define LAPIC_ID        0x020
#define LAPIC_EOI       0x0B0
#define LAPIC_SVR       0x0F0
#define LAPIC_ICR_LOW   0x300
#define LAPIC_ICR_HIGH  0x310
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_LVT_LINT0 0x350
#define LAPIC_LVT_LINT1 0x360
#define LAPIC_LVT_ERROR 0x370
#define LAPIC_TIMER_DIV 0x3E0
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CUR 0x390

#define SVR_ENABLE 0x100
#define LVT_MASKED  (1U << 16)

static volatile uint32_t *lapic_mmio;
static uint32_t timer_hz_cached = 100;
static uint32_t timer_initial_count = 1000000;

static uint32_t lapic_read(uint32_t reg)
{
    return lapic_mmio[reg / 4];
}

static void lapic_write(uint32_t reg, uint32_t value)
{
    lapic_mmio[reg / 4] = value;
}

static void lapic_wait_icr(void)
{
    while (lapic_read(LAPIC_ICR_LOW) & (1U << 12)) {
        __asm__ volatile("pause");
    }
}

static void lapic_soft_enable(void)
{
    uint32_t svr = lapic_read(LAPIC_SVR);
    svr |= SVR_ENABLE;
    svr = (svr & ~0xffU) | 0xffU; /* spurious vector */
    lapic_write(LAPIC_SVR, svr);
}

static void lapic_mask_lint(void)
{
    /* Block ExtINT/NMI from PIC virtual-wire so only this CPU's LAPIC IRQs apply. */
    lapic_write(LAPIC_LVT_LINT0, LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT1, LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR, LVT_MASKED);
}

void lapic_init(void)
{
    uint64_t apic_base = rdmsr(IA32_APIC_BASE_MSR);
    uint64_t phys = apic_base & 0xfffff000ULL;

    if (!(apic_base & APIC_BASE_ENABLE)) {
        apic_base |= APIC_BASE_ENABLE;
        wrmsr(IA32_APIC_BASE_MSR, apic_base);
    }

    lapic_mmio = (volatile uint32_t *)map_mmio(phys, 0x1000);
    if (!lapic_mmio) {
        panic("lapic: map failed");
    }
    lapic_soft_enable();
}

void lapic_ap_init(void)
{
    uint64_t apic_base = rdmsr(IA32_APIC_BASE_MSR);
    if (!(apic_base & APIC_BASE_ENABLE)) {
        apic_base |= APIC_BASE_ENABLE;
        wrmsr(IA32_APIC_BASE_MSR, apic_base);
    }
    if (!lapic_mmio) {
        panic("lapic: AP before BSP init");
    }
    lapic_soft_enable();
    lapic_mask_lint();
}

uint32_t lapic_id(void)
{
    return lapic_read(LAPIC_ID) >> 24;
}

void lapic_eoi(void)
{
    if (lapic_mmio) {
        lapic_write(LAPIC_EOI, 0);
    }
}

void lapic_send_ipi(uint32_t dest_lapic_id, uint8_t vector)
{
    if (!lapic_mmio) {
        return;
    }
    lapic_wait_icr();
    lapic_write(LAPIC_ICR_HIGH, dest_lapic_id << 24);
    /* Fixed delivery, edge, assert, destination physical */
    lapic_write(LAPIC_ICR_LOW, (uint32_t)vector);
    lapic_wait_icr();
}

void lapic_send_ipi_all_excluding_self(uint8_t vector)
{
    if (!lapic_mmio) {
        return;
    }
    lapic_wait_icr();
    /* Destination shorthand: all excluding self (2 << 18) */
    lapic_write(LAPIC_ICR_LOW, (2U << 18) | (uint32_t)vector);
    lapic_wait_icr();
}

static void lapic_timer_program(uint32_t hz)
{
    uint32_t count;
    if (hz == 0) {
        hz = 100;
    }
    timer_hz_cached = hz;
    /* Divide by 16 */
    lapic_write(LAPIC_TIMER_DIV, 0x3);
    /* Mask timer while calibrating / programming */
    lapic_write(LAPIC_LVT_TIMER, (1U << 16) | LAPIC_TIMER_VECTOR);

    if (timer_initial_count == 0) {
        timer_initial_count = 1000000;
    }
    count = timer_initial_count;
    lapic_write(LAPIC_TIMER_INIT, count);
    /* Periodic */
    lapic_write(LAPIC_LVT_TIMER, (1U << 17) | LAPIC_TIMER_VECTOR);
}

void lapic_timer_init(uint32_t hz)
{
    /*
     * Rough calibration against a short busy-wait. QEMU's LAPIC timer is
     * fast enough that a fixed count works; refine when PIT is still alive.
     */
    uint32_t start;
    uint32_t end;
    uint32_t ticks;
    uint32_t i;

    if (!lapic_mmio) {
        return;
    }

    lapic_write(LAPIC_TIMER_DIV, 0x3);
    lapic_write(LAPIC_LVT_TIMER, (1U << 16) | LAPIC_TIMER_VECTOR);
    lapic_write(LAPIC_TIMER_INIT, 0xffffffffU);

    /* Busy-wait ~10ms worth of iterations (architecture-dependent). */
    for (i = 0; i < 2000000U; i++) {
        __asm__ volatile("pause");
    }

    start = 0xffffffffU;
    end = lapic_read(LAPIC_TIMER_CUR);
    ticks = start - end;
    if (ticks < 1000U) {
        ticks = 1000000U;
    }
    /* Approximate: the busy loop ~10ms → counts per second = ticks * 100 */
    timer_initial_count = (ticks * 100U) / (hz ? hz : 100U);
    if (timer_initial_count < 1000U) {
        timer_initial_count = 1000U;
    }
    lapic_timer_program(hz);
}

void lapic_timer_ap_init(uint32_t hz)
{
    (void)hz;
    if (!lapic_mmio) {
        return;
    }
    /* APs schedule via reschedule IPI from the BSP timer; keep local timer masked. */
    lapic_write(LAPIC_TIMER_DIV, 0x3);
    lapic_write(LAPIC_LVT_TIMER, (1U << 16) | LAPIC_TIMER_VECTOR);
}

uint32_t lapic_timer_hz(void)
{
    return timer_hz_cached;
}
