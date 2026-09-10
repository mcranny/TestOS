#include "arch/tss.h"

static uint8_t ist_stacks[CPU_MAX][4096] __attribute__((aligned(16)));
static uint8_t rsp0_stacks[CPU_MAX][4096] __attribute__((aligned(16)));
static struct tss64 tss_table[CPU_MAX];

void tss_init_all(void)
{
    uint32_t cpu;
    for (cpu = 0; cpu < CPU_MAX; cpu++) {
        uint8_t *p = (uint8_t *)&tss_table[cpu];
        uint64_t i;
        for (i = 0; i < sizeof(struct tss64); i++) {
            p[i] = 0;
        }
        tss_table[cpu].rsp0 = (uint64_t)(rsp0_stacks[cpu] + sizeof(rsp0_stacks[cpu]));
        tss_table[cpu].ist1 = (uint64_t)(ist_stacks[cpu] + sizeof(ist_stacks[cpu]));
        tss_table[cpu].iopb = sizeof(struct tss64);
    }
}

struct tss64 *tss_get_for(uint32_t cpu)
{
    if (cpu >= CPU_MAX) {
        return NULL;
    }
    return &tss_table[cpu];
}

struct tss64 *tss_get(void)
{
    return tss_get_for(cpu_id());
}

void tss_set_kernel_stack(uint64_t rsp0)
{
    tss_get()->rsp0 = rsp0;
}
