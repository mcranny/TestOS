BITS 32

%define USER_CODE_SELECTOR 0x001B
%define USER_DATA_SELECTOR 0x0023

section .text

global process_switch_asm
global process_enter_user_mode

process_switch_asm:
    mov eax, [esp + 4]
    mov edx, [esp + 8]
    mov ecx, [esp + 12]

    test eax, eax
    jz .load

    pusha
    pushfd
    mov [eax], esp

.load:
    mov cr3, ecx
    mov esp, edx
    popfd
    popa
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ret

process_enter_user_mode:
    mov eax, [esp + 4]
    mov ecx, [esp + 8]

    mov dx, USER_DATA_SELECTOR
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx

    push USER_DATA_SELECTOR
    push ecx
    pushf
    push USER_CODE_SELECTOR
    push eax
    iret
