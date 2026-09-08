.section .text
.global _start
.extern main
_start:
    mov (%rsp), %rdi          /* argc */
    lea 8(%rsp), %rsi         /* argv */
    call main
    mov %rax, %rdi
    mov $1, %rax              /* SYS_EXIT */
    syscall
1:
    jmp 1b
