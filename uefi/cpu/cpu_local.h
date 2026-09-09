#ifndef TESTOS_UEFI_CPU_CPU_LOCAL_H
#define TESTOS_UEFI_CPU_CPU_LOCAL_H

#include "types.h"

struct process;

/*
 * Per-CPU state boundary (SMP prep).
 *
 * Only CPU0 is live today (cpu_id() always returns 0). Accessors exist so
 * scheduler / syscall / TSS paths stop treating a bare file-static `current`
 * as the global source of truth. When APs come online, cpu_id() will map to
 * an APIC/CPU index and each slot gets its own current, TSS/RSP0 mirror, etc.
 *
 * Not yet moved here (still global / documented in _tmp/SMP.md):
 *   - syscall live RSP slots (current_syscall_* in syscall_entry.S)
 *   - TSS / GDT (one per CPU later)
 *   - ready queue (needs a lock or per-CPU runqueues)
 */

#define CPU_MAX 8

typedef struct cpu_local {
    struct process *current;
} cpu_local_t;

void cpu_local_init(void);
uint32_t cpu_id(void);
uint32_t cpu_count(void);
cpu_local_t *cpu_local_this(void);
cpu_local_t *cpu_local_of(uint32_t id);

/* Preferred current-process accessors (per-CPU-ready). */
struct process *cpu_current(void);
void cpu_set_current(struct process *p);

#endif
