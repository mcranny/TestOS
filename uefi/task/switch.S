.section .text
.global process_switch
.global process_enter_user
.extern sched_after_switch
.type process_switch, @function
.type process_enter_user, @function

/* void process_switch(process_t *prev, process_t *next)
 * rdi = prev, rsi = next
 * Caller holds sched_lock with IRQs disabled.
 */
process_switch:
    test %rdi, %rdi
    jz 1f
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, 40(%rdi)
1:
    mov 48(%rsi), %rax
    mov %cr3, %rcx
    cmp %rax, %rcx
    je 2f
    mov %rax, %cr3
2:
    mov 40(%rsi), %rsp
    call sched_after_switch
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret

/* void process_enter_user(uint64_t entry, uint64_t user_stack)
 * rdi = entry, rsi = user_stack
 *
 * Kernel has GS_BASE == KERNEL_GS_BASE == cpu_local. Zero GS_BASE for ring 3
 * so SYSCALL/IRQ-from-user can swapgs to the cpu_local still in KERNEL_GS_BASE.
 * Keep IF clear until iretq: a timer IRQ with GS_BASE==0 and kernel CS would
 * skip swapgs and fault on %%gs cpu_local access.
 */
process_enter_user:
    cli
    mov $0xC0000101, %ecx       /* IA32_GS_BASE */
    xor %eax, %eax
    xor %edx, %edx
    wrmsr

    mov $0x1B, %ax              /* USER_DATA_SEL */
    mov %ax, %ds
    mov %ax, %es
    xor %eax, %eax
    mov %ax, %fs
    mov %ax, %gs

    pushq $0x1B                 /* SS */
    pushq %rsi                  /* RSP */
    pushq $0x202                /* RFLAGS (IF set on return to user) */
    pushq $0x23                 /* CS = USER_CODE_SEL */
    pushq %rdi                  /* RIP */
    iretq
