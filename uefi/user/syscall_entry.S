.section .text
.global syscall_entry
.extern syscall_dispatch
.type syscall_entry, @function

#include "cpu/cpu_local_offsets.h"

/*
 * Syscall entry with swapgs. GS_BASE becomes cpu_local for the duration.
 * Offsets must match cpu_local_t.
 */
syscall_entry:
    swapgs
    mov %rsp, %gs:CPU_LOCAL_OFF_SYSCALL_USER_RSP
    mov %gs:CPU_LOCAL_OFF_SYSCALL_KERN_RSP, %rsp

    push %r11
    push %rcx
    push %rax
    push %rdi
    push %rsi
    push %rdx
    push %r8
    push %r9
    push %r10
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15

    mov $0x10, %ax          /* KERNEL_DATA_SEL */
    mov %ax, %ds
    mov %ax, %es

    mov (12 * 8)(%rsp), %rdi
    mov (11 * 8)(%rsp), %rsi
    mov (10 * 8)(%rsp), %rdx
    mov (9 * 8)(%rsp), %rcx
    call syscall_dispatch
    mov %rax, (12 * 8)(%rsp)

    mov $0x1B, %ax          /* USER_DATA_SEL for return */
    mov %ax, %ds
    mov %ax, %es

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    pop %r10
    pop %r9
    pop %r8
    pop %rdx
    pop %rsi
    pop %rdi
    pop %rax
    pop %rcx
    pop %r11

    mov %gs:CPU_LOCAL_OFF_SYSCALL_USER_RSP, %rsp
    swapgs
    sysretq
