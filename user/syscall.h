#ifndef SYSCALL_H
#define SYSCALL_H

#include "syscall_abi.h"

typedef struct syscall_args
{
    uint32_t number;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
    uint32_t arg4;
    uint32_t arg5;
} syscall_args_t;

void syscall_initialize(void);
int32_t syscall_handler(const syscall_args_t *args);

#endif
