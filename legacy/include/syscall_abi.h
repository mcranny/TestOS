#ifndef SYSCALL_ABI_H
#define SYSCALL_ABI_H

/*
 * TestOS System Call ABI (version 1)
 *
 * Entry:  int 0x80  (vector 0x80, trap gate, DPL=3)
 *
 * Register calling convention:
 *   EAX = syscall number
 *   EBX = arg1
 *   ECX = arg2
 *   EDX = arg3
 *   ESI = arg4
 *   EDI = arg5
 *
 * Return:
 *   EAX >= 0  success (meaning depends on syscall)
 *   EAX < 0   negated errno code (see SYSCALL_ERR_*)
 *
 * User pointers must lie entirely within user-accessible pages of the
 * calling process address space. Kernel memory is never accessible.
 */

#include "types.h"

#define SYSCALL_VECTOR       0x80U
#define SYSCALL_ABI_VERSION  1U

#define SYS_EXIT             1U
#define SYS_WRITE            2U
#define SYS_READ             3U
#define SYS_OPEN             4U
#define SYS_CLOSE            5U
#define SYS_YIELD            6U
#define SYS_GETPID           7U
#define SYS_ABI_VERSION      8U
#define SYS_SLEEP            9U

#define SYSCALL_MAX          9U

#define SYSCALL_ERR_PERM     1
#define SYSCALL_ERR_NOENT    2
#define SYSCALL_ERR_BADF     3
#define SYSCALL_ERR_FAULT    14
#define SYSCALL_ERR_INVAL    22
#define SYSCALL_ERR_NOSYS    38

#define SYSCALL_MAX_WRITE    256U

#endif
