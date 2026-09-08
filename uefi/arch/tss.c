#include "arch/tss.h"

static uint8_t ist_stack[4096] __attribute__((aligned(16)));
static uint8_t rsp0_stack[4096] __attribute__((aligned(16)));
static struct tss64 tss;

void tss_init(void)
{
    uint8_t *p = (uint8_t *)&tss;
    uint64_t i;
    for (i = 0; i < sizeof(tss); i++) {
        p[i] = 0;
    }
    tss.rsp0 = (uint64_t)(rsp0_stack + sizeof(rsp0_stack));
    tss.ist1 = (uint64_t)(ist_stack + sizeof(ist_stack));
    tss.iopb = sizeof(struct tss64);
}

struct tss64 *tss_get(void)
{
    return &tss;
}

void tss_set_kernel_stack(uint64_t rsp0)
{
    tss.rsp0 = rsp0;
}
