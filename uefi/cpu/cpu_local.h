#ifndef TESTOS_UEFI_CPU_CPU_LOCAL_H
#define TESTOS_UEFI_CPU_CPU_LOCAL_H

#include "types.h"
#include "cpu/cpu_local_offsets.h"

struct process;

#define CPU_MAX 8

typedef struct cpu_local {
    uint32_t self;
    uint32_t lapic_id;
    struct process *current;
    struct process *idle;
    struct process *syscall_rsp_owner;
    uint64_t syscall_kernel_rsp;
    uint64_t syscall_user_rsp;
    volatile int need_resched;
} cpu_local_t;

_Static_assert(sizeof(cpu_local_t) >= 52, "cpu_local_t layout");
_Static_assert(__builtin_offsetof(cpu_local_t, self) == CPU_LOCAL_OFF_SELF, "self offset");
_Static_assert(__builtin_offsetof(cpu_local_t, lapic_id) == CPU_LOCAL_OFF_LAPIC_ID, "lapic offset");
_Static_assert(__builtin_offsetof(cpu_local_t, current) == CPU_LOCAL_OFF_CURRENT, "current offset");
_Static_assert(__builtin_offsetof(cpu_local_t, idle) == CPU_LOCAL_OFF_IDLE, "idle offset");
_Static_assert(__builtin_offsetof(cpu_local_t, syscall_rsp_owner) == CPU_LOCAL_OFF_SYSCALL_OWNER, "owner offset");
_Static_assert(__builtin_offsetof(cpu_local_t, syscall_kernel_rsp) == CPU_LOCAL_OFF_SYSCALL_KERN_RSP, "kern rsp offset");
_Static_assert(__builtin_offsetof(cpu_local_t, syscall_user_rsp) == CPU_LOCAL_OFF_SYSCALL_USER_RSP, "user rsp offset");
_Static_assert(__builtin_offsetof(cpu_local_t, need_resched) == CPU_LOCAL_OFF_NEED_RESCHED, "need_resched offset");

void cpu_local_init(void);
void cpu_local_set_count(uint32_t count);
void cpu_local_set_lapic(uint32_t id, uint32_t lapic_id);
void cpu_local_install_gs(uint32_t id);
uint32_t cpu_id(void);
uint32_t cpu_count(void);
cpu_local_t *cpu_local_this(void);
cpu_local_t *cpu_local_of(uint32_t id);

struct process *cpu_current(void);
void cpu_set_current(struct process *p);

#endif
