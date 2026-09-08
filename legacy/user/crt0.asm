BITS 32

extern main

global _start

; On entry ESP points to: argc, argv[0], argv[1], ..., NULL, <strings>
_start:
    xor ebp, ebp
    pop eax
    mov ecx, esp

    push ecx
    push eax
    call main

    mov ebx, eax
    mov eax, 1
    int 0x80

.hang:
    jmp .hang
