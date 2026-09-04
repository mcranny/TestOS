.section .text
.global _start
.type _start, @function
_start:
    cli
    cld
    call uefi_main
1:
    cli
    hlt
    jmp 1b
