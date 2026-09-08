BITS 32

section .multiboot
align 4

MULTIBOOT_MAGIC    equ 0x1BADB002
MULTIBOOT_FLAGS    equ 0x00000007
MULTIBOOT_CHECKSUM equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

dd MULTIBOOT_MAGIC
dd MULTIBOOT_FLAGS
dd MULTIBOOT_CHECKSUM
dd 0                       ; linear graphics mode requested
dd 1024                    ; preferred width
dd 768                     ; preferred height
dd 32                      ; preferred bits per pixel


section .note.Xen note
align 4
    dd 4
    dd 4
    dd 18
    db "Xen", 0
align 4
    dd _start
align 4


section .text

global _start
global stack_top
extern kernel_main

_start:
    mov esp, stack_top

    push ebx
    push eax
    call kernel_main
    add esp, 8

.hang:
    cli
    hlt
    jmp .hang


section .bss

align 16
stack_bottom:
    resb 16384
stack_top:
