#ifndef TESTOS_UEFI_CPU_CPU_LOCAL_OFFSETS_H
#define TESTOS_UEFI_CPU_CPU_LOCAL_OFFSETS_H

/* Shared with assembly — keep in sync with cpu_local_t in cpu_local.h */
#define CPU_LOCAL_OFF_SELF             0
#define CPU_LOCAL_OFF_LAPIC_ID         4
#define CPU_LOCAL_OFF_CURRENT          8
#define CPU_LOCAL_OFF_IDLE             16
#define CPU_LOCAL_OFF_SYSCALL_OWNER    24
#define CPU_LOCAL_OFF_SYSCALL_KERN_RSP 32
#define CPU_LOCAL_OFF_SYSCALL_USER_RSP 40
#define CPU_LOCAL_OFF_NEED_RESCHED     48

#endif
