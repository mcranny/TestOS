#ifndef TESTOS_UEFI_ARCH_IDT_H
#define TESTOS_UEFI_ARCH_IDT_H

#include "types.h"

struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

void idt_init(void);
void idt_set_gate(uint8_t vector, void *handler, uint8_t ist, uint8_t type_attr);

#endif
