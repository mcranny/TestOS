#include "cpu/cpu_local.h"

static cpu_local_t cpu_locals[CPU_MAX];
static uint32_t ncpus = 1;

void cpu_local_init(void)
{
    uint32_t i;
    for (i = 0; i < CPU_MAX; i++) {
        cpu_locals[i].current = NULL;
    }
    ncpus = 1; /* BSP only until AP bring-up */
}

uint32_t cpu_id(void)
{
    /* Stub: LAPIC ID / GS-based CPU index when SMP starts. */
    return 0;
}

uint32_t cpu_count(void)
{
    return ncpus;
}

cpu_local_t *cpu_local_this(void)
{
    return &cpu_locals[cpu_id()];
}

cpu_local_t *cpu_local_of(uint32_t id)
{
    if (id >= CPU_MAX) {
        return NULL;
    }
    return &cpu_locals[id];
}

struct process *cpu_current(void)
{
    return cpu_local_this()->current;
}

void cpu_set_current(struct process *p)
{
    cpu_local_this()->current = p;
}
