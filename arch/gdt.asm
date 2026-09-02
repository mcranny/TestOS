BITS 32

section .text

global gdt_load
global gdt_flush_segments

gdt_load:
    mov eax, [esp + 4]
    lgdt [eax]
    ret

gdt_flush_segments:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush_code
.flush_code:
    ret
