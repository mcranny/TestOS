.section .text
.global syscall_entry
.extern syscall_dispatch
.type syscall_entry, @function

/*
 * Syscall entry (no swapgs — single-CPU hobby kernel).
 * On entry: rax=nr, rdi/rsi/rdx=args, rcx=user RIP, r11=user RFLAGS, rsp=user RSP.
 */
syscall_entry:
    mov %rsp, current_syscall_user_rsp(%rip)
    mov current_syscall_kernel_rsp(%rip), %rsp

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

    /* Stack after 15 pushes:
     * +0 r15 ... +12*8 = rax(nr), +11*8=rdi, +10*8=rsi, +9*8=rdx
     */
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

    mov current_syscall_user_rsp(%rip), %rsp
    sysretq

.section .bss
.global current_syscall_kernel_rsp
.global current_syscall_user_rsp
.align 8
current_syscall_kernel_rsp:
    .quad 0
current_syscall_user_rsp:
    .quad 0
