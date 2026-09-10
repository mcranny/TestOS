#ifndef TESTOS_UEFI_ARCH_TSS_H
#define TESTOS_UEFI_ARCH_TSS_H

#include "types.h"
#include "cpu/cpu_local.h"

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;
} __attribute__((packed));

void tss_init_all(void);
struct tss64 *tss_get_for(uint32_t cpu);
struct tss64 *tss_get(void);
void tss_set_kernel_stack(uint64_t rsp0);

#endif
