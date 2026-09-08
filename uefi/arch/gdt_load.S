.section .text
.global gdt_load
.type gdt_load, @function
/* void gdt_load(const struct gdt_ptr *gdtr, uint16_t code_sel, uint16_t data_sel) */
gdt_load:
    lgdt (%rdi)
    mov %dx, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    pushq %rsi
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    ret
