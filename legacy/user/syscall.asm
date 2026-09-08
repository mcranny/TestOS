BITS 32

%include "syscall_abi.inc"

section .text

global syscall_stub
extern syscall_handler

%define SYSCALL_ERR_PERM_VALUE (-1)

; Stack layout after: push ds/es/fs/gs, pusha
;   [esp+0]  EDI
;   [esp+4]  ESI
;   [esp+8]  EBP
;   [esp+12] ESP
;   [esp+16] EBX
;   [esp+20] EDX
;   [esp+24] ECX
;   [esp+28] EAX   <- syscall number / return value
;   [esp+32] GS
;   [esp+36] FS
;   [esp+40] ES
;   [esp+44] DS
;   [esp+48] EIP
;   [esp+52] CS
;   [esp+56] EFLAGS
;   [esp+60] user ESP
;   [esp+64] user SS

syscall_stub:
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

    ; Reject syscalls not invoked from ring 3.
    mov eax, [esp + 52]
    and eax, 3
    cmp eax, 3
    jne .reject

    ; Syscall number is the user EAX saved by pusha (register EAX was clobbered).
    mov eax, [esp + 28]

    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax
    mov eax, esp
    push eax
    call syscall_handler
    add esp, 28

    ; Return value goes back in user EAX.
    mov [esp + 28], eax
    jmp .return

.reject:
    mov dword [esp + 28], SYSCALL_ERR_PERM_VALUE

.return:
    popa
    pop gs
    pop fs
    pop es
    pop ds
    iret
