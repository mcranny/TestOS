BITS 32

section .text

extern exception_handler
extern exception_need_reschedule
extern exception_reschedule

%macro ISR_NOERR 1
global exception_%1
exception_%1:
    cli
    push dword 0
    push dword %1
    jmp exception_common
%endmacro

%macro ISR_ERR 1
global exception_%1
exception_%1:
    cli
    push dword %1
    jmp exception_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_NOERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

exception_common:
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

    push esp
    call exception_handler
    add esp, 4

    cmp dword [exception_need_reschedule], 0
    je .return

    mov dword [exception_need_reschedule], 0
    ; Abandon this exception frame; never iret to the faulting user EIP.
    call exception_reschedule
    ; Not reached.
.hang:
    cli
    hlt
    jmp .hang

.return:
    popa
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 8
    iret
