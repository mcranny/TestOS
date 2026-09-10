#include "cpu/cpu_local.h"
#include "arch/io.h"

#define IA32_GS_BASE        0xC0000101U
#define IA32_KERNEL_GS_BASE 0xC0000102U

static cpu_local_t cpu_locals[CPU_MAX];
static uint32_t ncpus = 1;

void cpu_local_init(void)
{
    uint32_t i;
    for (i = 0; i < CPU_MAX; i++) {
        cpu_locals[i].self = i;
        cpu_locals[i].lapic_id = 0;
        cpu_locals[i].current = NULL;
        cpu_locals[i].idle = NULL;
        cpu_locals[i].syscall_rsp_owner = NULL;
        cpu_locals[i].syscall_kernel_rsp = 0;
        cpu_locals[i].syscall_user_rsp = 0;
        cpu_locals[i].need_resched = 0;
    }
    ncpus = 1;
}

void cpu_local_set_count(uint32_t count)
{
    if (count == 0) {
        count = 1;
    }
    if (count > CPU_MAX) {
        count = CPU_MAX;
    }
    ncpus = count;
}

void cpu_local_set_lapic(uint32_t id, uint32_t lapic_id)
{
    if (id >= CPU_MAX) {
        return;
    }
    cpu_locals[id].lapic_id = lapic_id;
}

void cpu_local_install_gs(uint32_t id)
{
    uint64_t base;
    if (id >= CPU_MAX) {
        return;
    }
    base = (uint64_t)(uintptr_t)&cpu_locals[id];
    /*
     * Keep both MSRs equal while in kernel so a mistaken swapgs (e.g. if CS
     * is mis-read in the IRQ stub) is a no-op. process_enter_user zeroes
     * GS_BASE for ring 3; SYSCALL/IRQ-from-user swapgs then still works.
     */
    wrmsr(IA32_GS_BASE, base);
    wrmsr(IA32_KERNEL_GS_BASE, base);
}

void cpu_local_prepare_user(void)
{
    uint32_t id = cpu_id();
    uint64_t base = (uint64_t)(uintptr_t)&cpu_locals[id];
    /*
     * IRQ-from-user does swapgs (GS=cpu_local, KERNEL_GS=0). A context switch
     * can leave that asymmetric state. Zeroing only GS_BASE would then clear
     * both MSRs and the next user IRQ/syscall faults at %%gs:0x28.
     * Reinstall KERNEL_GS first, then clear GS for ring 3.
     */
    wrmsr(IA32_KERNEL_GS_BASE, base);
    wrmsr(IA32_GS_BASE, 0);
}

uint32_t cpu_id(void)
{
    uint32_t id;
    __asm__ volatile("mov %%gs:%c1, %0"
                     : "=r"(id)
                     : "i"(CPU_LOCAL_OFF_SELF));
    return id;
}

uint32_t cpu_count(void)
{
    return ncpus;
}

cpu_local_t *cpu_local_this(void)
{
    return &cpu_locals[cpu_id()];
}

cpu_local_t *cpu_local_of(uint32_t id)
{
    if (id >= CPU_MAX) {
        return NULL;
    }
    return &cpu_locals[id];
}

struct process *cpu_current(void)
{
    return cpu_local_this()->current;
}

void cpu_set_current(struct process *p)
{
    cpu_local_this()->current = p;
}
