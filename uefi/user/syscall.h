#ifndef TESTOS_UEFI_USER_SYSCALL_H
#define TESTOS_UEFI_USER_SYSCALL_H

#include "types.h"

#define SYS_EXIT   1
#define SYS_WRITE  2
#define SYS_YIELD  6
#define SYS_GETPID 7
#define SYS_SLEEP  9

void syscall_set_kernel_rsp(uint64_t rsp);
void syscall_init(void);
int64_t syscall_dispatch(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2);

#endif
