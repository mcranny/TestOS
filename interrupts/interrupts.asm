BITS 32

section .text

global timer_irq_stub
extern timer_handler

global keyboard_irq_stub
extern keyboard_handler

timer_irq_stub:
    push ds
    push es
    push fs
    push gs
    pusha

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call timer_handler

    popa
    pop gs
    pop fs
    pop es
    pop ds
    iret

keyboard_irq_stub:
    push ds
    push es
    push fs
    push gs
    pusha

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call keyboard_handler

    popa
    pop gs
    pop fs
    pop es
    pop ds
    iret
