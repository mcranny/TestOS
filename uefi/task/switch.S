.section .text
.global process_switch
.global process_enter_user
.type process_switch, @function
.type process_enter_user, @function

/* void process_switch(process_t *prev, process_t *next)
 * rdi = prev, rsi = next
 * Saves callee-saved regs + rsp into prev->kernel_rsp (offset 40),
 * loads next->kernel_rsp, switches CR3 from next->cr3 (offset 48).
 *
 * process_t layout (must match process.h):
 *  0 pid (u32)
 *  4 state (u32)
 *  8 name[32]
 * 40 kernel_rsp (u64)
 * 48 cr3 (u64)
 * ... trailing fields (incl. syscall_user_rsp) must not shift 40/48.
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
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret

/* void process_enter_user(uint64_t entry, uint64_t user_stack)
 * rdi = entry, rsi = user_stack
 * Builds iretq frame for ring3.
 */
process_enter_user:
    mov $0x1B, %ax          /* USER_DATA_SEL */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    pushq $0x1B             /* SS */
    pushq %rsi              /* RSP */
    pushq $0x202            /* RFLAGS (IF set) */
    pushq $0x23             /* CS = USER_CODE_SEL */
    pushq %rdi              /* RIP */
    iretq
