#include "cpu/smp.h"
#include "cpu/cpu_local.h"
#include "arch/apic.h"
#include "arch/gdt.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "mm/paging.h"
#include "task/process.h"
#include "drivers/console.h"
#include "platform.h"

static struct limine_smp_response *smp_response;
static volatile uint32_t cpus_online = 1;
static volatile uint32_t smp_scheduling = 0;
static uint32_t bsp_lapic_id;

void smp_ap_entry(struct limine_smp_info *info)
{
    uint32_t id;
    uint64_t cr3;
    char msg[20];

    irq_disable();

    if (!info) {
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    id = (uint32_t)info->extra_argument;
    if (id == 0 || id >= cpu_count()) {
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    cr3 = address_space_kernel()->pml4_phys;
    write_cr3(cr3);

    idt_reload();
    gdt_load_cpu(id);
    lapic_ap_init();
    lapic_timer_ap_init(lapic_timer_hz());

    __atomic_add_fetch(&cpus_online, 1, __ATOMIC_SEQ_CST);

    msg[0] = 'A'; msg[1] = 'P'; msg[2] = ' ';
    msg[3] = 'o'; msg[4] = 'n'; msg[5] = 'l'; msg[6] = 'i';
    msg[7] = 'n'; msg[8] = 'e'; msg[9] = ' ';
    msg[10] = 'c'; msg[11] = 'p'; msg[12] = 'u'; msg[13] = '=';
    msg[14] = '0' + (char)(id % 10);
    msg[15] = '\n';
    msg[16] = 0;
    console_puts(msg);

    /* Park with IF clear; BSP still owns all work for this release. */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void smp_init(struct limine_smp_response *response)
{
    uint64_t i;
    uint32_t next = 1;

    smp_response = response;
    bsp_lapic_id = lapic_id();

    if (!response || response->cpu_count == 0 || !response->cpus) {
        cpu_local_set_count(1);
        cpu_local_set_lapic(0, bsp_lapic_id);
        return;
    }

    bsp_lapic_id = response->bsp_lapic_id;
    cpu_local_set_lapic(0, bsp_lapic_id);

    for (i = 0; i < response->cpu_count && next < CPU_MAX; i++) {
        struct limine_smp_info *info = response->cpus[i];
        if (!info) {
            continue;
        }
        if (info->lapic_id == bsp_lapic_id) {
            info->extra_argument = 0;
            continue;
        }
        cpu_local_set_lapic(next, info->lapic_id);
        info->extra_argument = next;
        next++;
    }
    cpu_local_set_count(next);
}

void smp_start_aps(void)
{
    uint64_t i;
    uint64_t expected;
    uint64_t spins = 0;

    if (!smp_response || smp_response->cpu_count <= 1 || !smp_response->cpus) {
        return;
    }

    expected = cpu_count();
    for (i = 0; i < smp_response->cpu_count; i++) {
        struct limine_smp_info *info = smp_response->cpus[i];
        if (!info || info->lapic_id == bsp_lapic_id) {
            continue;
        }
        if (info->extra_argument == 0 || info->extra_argument >= expected) {
            continue;
        }
        __atomic_store_n(&info->goto_address, smp_ap_entry, __ATOMIC_SEQ_CST);
    }

    while (__atomic_load_n(&cpus_online, __ATOMIC_SEQ_CST) < expected && spins < 100000000ULL) {
        __asm__ volatile("pause");
        spins++;
    }

    console_puts("smp cpus_online=");
    console_write_hex64(cpus_online);
    console_puts("/");
    console_write_hex64(expected);
    console_puts("\n");
}

void smp_enable_scheduling(void)
{
    __atomic_store_n(&smp_scheduling, 1, __ATOMIC_RELEASE);
}

uint32_t smp_cpus_online(void)
{
    return __atomic_load_n(&cpus_online, __ATOMIC_SEQ_CST);
}

int smp_scheduling_enabled(void)
{
    return __atomic_load_n(&smp_scheduling, __ATOMIC_ACQUIRE) != 0;
}
