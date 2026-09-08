BITS 32

section .text

global tss_load

tss_load:
    mov ax, [esp + 4]
    ltr ax
    ret
