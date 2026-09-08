; TestOS syscall ABI - NASM include (must match include/syscall_abi.h)

%ifndef SYSCALL_ABI_INC
%define SYSCALL_ABI_INC

%define SYSCALL_VECTOR       0x80
%define SYSCALL_ABI_VERSION  1

%define SYS_EXIT             1
%define SYS_WRITE            2
%define SYS_READ             3
%define SYS_OPEN             4
%define SYS_CLOSE            5
%define SYS_YIELD            6
%define SYS_GETPID           7
%define SYS_ABI_VERSION      8
%define SYS_SLEEP            9

%define SYSCALL_MAX          9

%define SYSCALL_ERR_PERM     1
%define SYSCALL_ERR_FAULT    14
%define SYSCALL_ERR_INVAL    22
%define SYSCALL_ERR_NOSYS    38

; SYSCALLn number, arg1, arg2, ...
%macro SYSCALL0 1
    mov eax, %1
    int SYSCALL_VECTOR
%endmacro

%macro SYSCALL1 2
    mov eax, %1
    mov ebx, %2
    int SYSCALL_VECTOR
%endmacro

%macro SYSCALL2 3
    mov eax, %1
    mov ebx, %2
    mov ecx, %3
    int SYSCALL_VECTOR
%endmacro

%macro SYSCALL3 4
    mov eax, %1
    mov ebx, %2
    mov ecx, %3
    mov edx, %4
    int SYSCALL_VECTOR
%endmacro

%endif
