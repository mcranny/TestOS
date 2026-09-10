#include "task/process.h"
#include "user/syscall.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "arch/tss.h"
#include "arch/io.h"
#include "arch/apic.h"
#include "cpu/cpu_local.h"
#include "cpu/smp.h"
#include "sync/spinlock.h"
#include "drivers/console.h"
#include "lib/string.h"
#include "platform.h"
#include "socket.h"

static process_t table[PROCESS_MAX];
static process_t *ready_head;
static process_t *ready_tail;
static uint32_t next_pid = 1;
static int scheduler_ready;
static int scheduler_preempt;
static spinlock_t sched_lock = SPINLOCK_INIT;

static process_t *process_create_kernel_ex(process_entry_t entry, const char *name,
                                          uint32_t parent_pid, int enqueue);
static void ready_enqueue_locked(process_t *p);
static void switch_to(process_t *next, uint64_t irq_flags);

/* Called on next's stack with sched_lock still held (IRQs off). */
void sched_after_switch(process_t *prev)
{
    if (prev && prev->state == PROC_READY) {
        ready_enqueue_locked(prev);
    }
    /* Keep IRQs disabled; callers/entries enable IF when safe. */
    spin_unlock(&sched_lock);
}

static void name_copy(char *dst, const char *src)
{
    size_t i = 0;
    if (!src) src = "?";
    while (src[i] && i + 1 < PROCESS_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int is_idle_process(process_t *p)
{
    return p && p->protected && p->name[0] == 'i' && p->name[1] == 'd';
}

static void ready_enqueue_locked(process_t *p)
{
    p->next = NULL;
    p->state = PROC_READY;
    if (!ready_tail) {
        ready_head = ready_tail = p;
    } else {
        ready_tail->next = p;
        ready_tail = p;
    }
}

static process_t *ready_dequeue_locked(void)
{
    process_t *p = ready_head;
    if (!p) {
        return NULL;
    }
    ready_head = p->next;
    if (!ready_head) {
        ready_tail = NULL;
    }
    p->next = NULL;
    return p;
}

static process_t *alloc_slot(void)
{
    uint32_t i;
    for (i = 0; i < PROCESS_MAX; i++) {
        if (table[i].state == PROC_UNUSED) {
            memset(&table[i], 0, sizeof(table[i]));
            return &table[i];
        }
    }
    return NULL;
}

void syscall_set_kernel_rsp(uint64_t rsp);

static void set_kernel_stacks(process_t *p)
{
    uint64_t top;
    cpu_local_t *cpu;
    if (!p || !p->kernel_stack) return;

    cpu = cpu_local_this();
    if (cpu->syscall_rsp_owner != p) {
        if (cpu->syscall_rsp_owner) {
            cpu->syscall_rsp_owner->syscall_user_rsp = cpu->syscall_user_rsp;
        }
        cpu->syscall_user_rsp = p->syscall_user_rsp;
        cpu->syscall_rsp_owner = p;
    }

    top = ((uint64_t)p->kernel_stack + PROCESS_KERNEL_STACK_SIZE + 15) & ~15ULL;
    tss_set_kernel_stack(top);
    syscall_set_kernel_rsp(top);
}

static void prepare_kernel_stack(process_t *p, process_entry_t entry)
{
    uint64_t *stack;
    uint64_t top;
    p->kernel_stack = (uint8_t *)kmalloc(PROCESS_KERNEL_STACK_SIZE + 16);
    if (!p->kernel_stack) {
        panic("process: kernel stack alloc failed");
    }
    top = ((uint64_t)p->kernel_stack + PROCESS_KERNEL_STACK_SIZE + 15) & ~15ULL;
    stack = (uint64_t *)top;
    *--stack = (uint64_t)entry;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    p->kernel_rsp = (uint64_t)stack;
}

static void user_trampoline(void)
{
    process_t *p = process_get_current();
    set_kernel_stacks(p);
    process_enter_user(p->user_entry, p->user_stack);
    process_exit(0);
}

static void idle_thread(void)
{
    for (;;) {
        irq_enable();
        process_reap_zombies();
        __asm__ volatile("hlt");
        scheduler_yield();
    }
}

static void worker_a(void)
{
    irq_enable();
    console_puts("scheduler worker A\n");
    process_exit(0);
}

static void worker_b(void)
{
    irq_enable();
    console_puts("scheduler worker B\n");
    process_exit(0);
}

void scheduler_kick_others(void)
{
    if (cpu_count() > 1 && smp_scheduling_enabled()) {
        lapic_send_ipi_all_excluding_self(IPI_VECTOR_RESCHED);
    }
}

static void ready_enqueue(process_t *p)
{
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    ready_enqueue_locked(p);
    spin_unlock_irqrestore(&sched_lock, flags);
    scheduler_kick_others();
}

void scheduler_init(void)
{
    process_t *shell;
    process_t *idle;
    uint32_t i;

    for (i = 0; i < PROCESS_MAX; i++) {
        table[i].state = PROC_UNUSED;
    }
    ready_head = ready_tail = NULL;
    next_pid = 1;

    shell = alloc_slot();
    shell->pid = next_pid++;
    shell->state = PROC_RUNNING;
    shell->time_slice = PROCESS_TIME_SLICE;
    shell->parent_pid = 0;
    shell->as = address_space_kernel();
    shell->cr3 = shell->as->pml4_phys;
    shell->is_user = 0;
    name_copy(shell->name, "shell");
    shell->protected = 1;
    shell->kernel_stack = (uint8_t *)kmalloc(PROCESS_KERNEL_STACK_SIZE + 16);
    if (!shell->kernel_stack) {
        panic("scheduler: shell stack alloc failed");
    }
    cpu_set_current(shell);
    set_kernel_stacks(shell);

    idle = process_create_kernel_ex(idle_thread, "idle", shell->pid, 0);
    if (idle) {
        idle->protected = 1;
        cpu_local_of(0)->idle = idle;
    }
    (void)process_create_kernel(worker_a, "worker-a", shell->pid);
    (void)process_create_kernel(worker_b, "worker-b", shell->pid);

    scheduler_ready = 1;
    console_puts("scheduler OK\n");
}

void scheduler_ap_online(uint32_t cpu)
{
    process_t *idle;
    char name[PROCESS_NAME_MAX];
    cpu_local_t *cl = cpu_local_of(cpu);
    uint64_t flags;
    if (!cl) {
        panic("scheduler_ap_online: bad cpu");
    }

    name_copy(name, "idle");
    idle = process_create_kernel_ex(idle_thread, name, 0, 0);
    if (!idle) {
        panic("scheduler_ap_online: idle alloc failed");
    }
    idle->protected = 1;
    cl->idle = idle;

    flags = spin_lock_irqsave(&sched_lock);
    idle->state = PROC_RUNNING;
    idle->time_slice = PROCESS_TIME_SLICE;
    cpu_set_current(idle);
    spin_unlock_irqrestore(&sched_lock, flags);
    set_kernel_stacks(idle);
}

void scheduler_ap_idle(void)
{
    process_t *idle = cpu_current();
    uint64_t rsp;

    if (!idle || !idle->kernel_rsp) {
        panic("scheduler_ap_idle: no idle");
    }

    /*
     * Drop onto the prepared idle stack without process_switch / sched_lock.
     * First entry has no previous task to save; avoid the shared unlock path.
     */
    rsp = idle->kernel_rsp;
    irq_disable();
    __asm__ volatile(
        "mov %0, %%rsp\n\t"
        "pop %%r15\n\t"
        "pop %%r14\n\t"
        "pop %%r13\n\t"
        "pop %%r12\n\t"
        "pop %%rbx\n\t"
        "pop %%rbp\n\t"
        "ret\n\t"
        :
        : "r"(rsp)
        : "memory");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void switch_to(process_t *next, uint64_t irq_flags)
{
    process_t *prev = cpu_current();
    if (!next || next == prev) {
        spin_unlock_irqrestore(&sched_lock, irq_flags);
        return;
    }

    /* Caller must hold sched_lock (IRQs off). */
    if (prev && prev->state == PROC_RUNNING) {
        if (is_idle_process(prev)) {
            prev->state = PROC_SLEEPING;
        } else {
            prev->state = PROC_READY;
        }
    }
    next->state = PROC_RUNNING;
    next->time_slice = PROCESS_TIME_SLICE;
    cpu_set_current(next);
    cpu_local_this()->need_resched = 0;
    set_kernel_stacks(next);
    (void)irq_flags;
    process_switch(prev, next);
    /* Resumed with IF still clear — correct for IRQ-return paths. */
}

void scheduler_tick(void)
{
    process_t *p;
    process_t *cur;
    uint64_t flags;
    int kick = 0;

    cur = cpu_current();
    if (!scheduler_ready || !cur) return;

    flags = spin_lock_irqsave(&sched_lock);
    for (p = &table[0]; p < &table[PROCESS_MAX]; p++) {
        if (p->state == PROC_SLEEPING && !is_idle_process(p) &&
            timer_ticks() >= p->wake_tick) {
            ready_enqueue_locked(p);
            cpu_local_this()->need_resched = 1;
            kick = 1;
        }
    }

    if (cur->time_slice > 0) {
        cur->time_slice--;
    }
    if (cur->time_slice == 0) {
        if (ready_head != NULL) {
            cpu_local_this()->need_resched = 1;
            kick = 1;
        } else {
            cur->time_slice = PROCESS_TIME_SLICE;
        }
    }
    spin_unlock_irqrestore(&sched_lock, flags);
    if (kick) {
        scheduler_kick_others();
    }
}

void scheduler_yield(void)
{
    process_t *next;
    process_t *cur;
    process_t *idle;
    uint64_t flags;
    if (!scheduler_ready) return;

    flags = spin_lock_irqsave(&sched_lock);
    next = ready_dequeue_locked();
    cur = cpu_current();
    idle = cpu_local_this()->idle;
    if (!next) {
        if (cur && is_idle_process(cur)) {
            cur->time_slice = PROCESS_TIME_SLICE;
            cpu_local_this()->need_resched = 0;
            spin_unlock_irqrestore(&sched_lock, flags);
            return;
        }
        if (idle && idle != cur) {
            next = idle;
        } else {
            if (cur) {
                cur->time_slice = PROCESS_TIME_SLICE;
            }
            cpu_local_this()->need_resched = 0;
            spin_unlock_irqrestore(&sched_lock, flags);
            return;
        }
    }
    cpu_local_this()->need_resched = 0;
    switch_to(next, flags);
}

void scheduler_on_irq_exit(void)
{
    if (!scheduler_preempt) return;
    if (!cpu_local_this()->need_resched) return;
    scheduler_yield();
    /* Stay with IF clear so iretq restores RFLAGS from the interrupt frame. */
    irq_disable();
}

void scheduler_enable_preempt(void)
{
    scheduler_preempt = 1;
}

process_t *process_get_current(void)
{
    return cpu_current();
}

uint32_t process_get_current_pid(void)
{
    process_t *cur = cpu_current();
    return cur ? cur->pid : 0;
}

process_t *process_find_by_pid(uint32_t pid)
{
    uint32_t i;
    for (i = 0; i < PROCESS_MAX; i++) {
        if (table[i].state != PROC_UNUSED && table[i].pid == pid) {
            return &table[i];
        }
    }
    return NULL;
}

uint32_t process_table_count(void)
{
    return PROCESS_MAX;
}

process_t *process_at(uint32_t index)
{
    if (index >= PROCESS_MAX) return NULL;
    return &table[index];
}

process_t *process_create_kernel(process_entry_t entry, const char *name, uint32_t parent_pid)
{
    return process_create_kernel_ex(entry, name, parent_pid, 1);
}

static process_t *process_create_kernel_ex(process_entry_t entry, const char *name,
                                          uint32_t parent_pid, int enqueue)
{
    process_t *p;
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    p = alloc_slot();
    if (!p) {
        spin_unlock_irqrestore(&sched_lock, flags);
        return NULL;
    }
    p->pid = next_pid++;
    p->parent_pid = parent_pid;
    p->as = address_space_kernel();
    p->cr3 = p->as->pml4_phys;
    p->is_user = 0;
    p->time_slice = PROCESS_TIME_SLICE;
    name_copy(p->name, name);
    spin_unlock_irqrestore(&sched_lock, flags);

    prepare_kernel_stack(p, entry);
    if (enqueue) {
        ready_enqueue(p);
    } else {
        p->state = PROC_SLEEPING;
    }
    return p;
}

process_t *process_create_user(uint64_t entry, uint64_t user_stack, address_space_t *as,
                               const char *name, uint32_t parent_pid)
{
    process_t *p;
    uint64_t flags;
    if (!as) return NULL;

    flags = spin_lock_irqsave(&sched_lock);
    p = alloc_slot();
    if (!p) {
        spin_unlock_irqrestore(&sched_lock, flags);
        return NULL;
    }
    p->pid = next_pid++;
    p->parent_pid = parent_pid;
    p->as = as;
    p->cr3 = as->pml4_phys;
    p->is_user = 1;
    p->user_entry = entry;
    p->user_stack = user_stack;
    p->time_slice = PROCESS_TIME_SLICE;
    name_copy(p->name, name);
    spin_unlock_irqrestore(&sched_lock, flags);

    prepare_kernel_stack(p, user_trampoline);
    ready_enqueue(p);
    return p;
}

void process_exit(int32_t code)
{
    process_t *p = cpu_current();
    process_t *next;
    process_t *candidate;
    process_t **pp;
    process_t *prev_link;
    process_t *idle;
    uint64_t flags;
    if (!p) return;

    socket_close_owned_by(p->pid);

    flags = spin_lock_irqsave(&sched_lock);
    p->exit_code = code;
    p->state = PROC_ZOMBIE;

    next = NULL;
    pp = &ready_head;
    prev_link = NULL;
    while (*pp) {
        candidate = *pp;
        if (!is_idle_process(candidate)) {
            *pp = candidate->next;
            if (ready_tail == candidate) ready_tail = prev_link;
            candidate->next = NULL;
            next = candidate;
            break;
        }
        prev_link = candidate;
        pp = &candidate->next;
    }
    if (!next) {
        next = ready_dequeue_locked();
    }
    idle = cpu_local_this()->idle;
    if (!next && idle && idle != p) {
        next = idle;
    }
    if (!next) {
        spin_unlock_irqrestore(&sched_lock, flags);
        for (;;) __asm__ volatile("hlt");
    }
    cpu_set_current(next);
    next->state = PROC_RUNNING;
    next->time_slice = PROCESS_TIME_SLICE;
    set_kernel_stacks(next);
    process_switch(NULL, next);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

int process_terminate(uint32_t pid)
{
    process_t *p = process_find_by_pid(pid);
    uint64_t flags;
    if (!p || p->pid == 0) return -1;
    if (p->protected) return -1;
    if (p == cpu_current()) {
        process_exit(137);
        return 0;
    }
    if (p->state == PROC_UNUSED || p->state == PROC_ZOMBIE) return -1;

    socket_close_owned_by(p->pid);

    flags = spin_lock_irqsave(&sched_lock);
    {
        process_t **pp = &ready_head;
        process_t *prev_tail_check = NULL;
        while (*pp) {
            if (*pp == p) {
                *pp = p->next;
                if (ready_tail == p) ready_tail = prev_tail_check;
                break;
            }
            prev_tail_check = *pp;
            pp = &(*pp)->next;
        }
    }
    p->state = PROC_ZOMBIE;
    p->exit_code = 137;
    spin_unlock_irqrestore(&sched_lock, flags);
    return 0;
}

void process_sleep_ticks(uint64_t ticks)
{
    process_t *next;
    process_t *cur;
    process_t *idle;
    uint64_t flags;
    cur = cpu_current();
    if (!cur) return;

    flags = spin_lock_irqsave(&sched_lock);
    cur->wake_tick = timer_ticks() + ticks;
    cur->state = PROC_SLEEPING;
    next = ready_dequeue_locked();
    idle = cpu_local_this()->idle;
    if (!next && idle && idle != cur) {
        next = idle;
    }
    if (!next) {
        cur->state = PROC_RUNNING;
        spin_unlock_irqrestore(&sched_lock, flags);
        return;
    }
    switch_to(next, flags);
    irq_enable();
}

void process_reap_zombies(void)
{
    uint32_t i;
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    for (i = 0; i < PROCESS_MAX; i++) {
        process_t *p = &table[i];
        if (p->state != PROC_ZOMBIE) continue;
        spin_unlock_irqrestore(&sched_lock, flags);

        if (p->is_user && p->as && p->as != address_space_kernel()) {
            address_space_destroy(p->as);
            p->as = NULL;
        }
        if (p->kernel_stack) {
            kfree(p->kernel_stack);
            p->kernel_stack = NULL;
        }

        flags = spin_lock_irqsave(&sched_lock);
        p->state = PROC_UNUSED;
    }
    spin_unlock_irqrestore(&sched_lock, flags);
}

void process_wait_pid(uint32_t pid)
{
    for (;;) {
        process_t *p = process_find_by_pid(pid);
        process_t *next;
        process_t **pp;
        process_t *prev_link;
        process_t *idle;

        if (!p || p->state == PROC_ZOMBIE || p->state == PROC_UNUSED) {
            process_reap_zombies();
            return;
        }

        if (p->state == PROC_READY) {
            uint64_t flags = spin_lock_irqsave(&sched_lock);
            if (p->state == PROC_READY) {
                pp = &ready_head;
                prev_link = NULL;
                while (*pp) {
                    if (*pp == p) {
                        *pp = p->next;
                        if (ready_tail == p) ready_tail = prev_link;
                        p->next = NULL;
                        break;
                    }
                    prev_link = *pp;
                    pp = &(*pp)->next;
                }
                switch_to(p, flags);
                irq_enable();
                continue;
            }
            spin_unlock_irqrestore(&sched_lock, flags);
            continue;
        }

        {
            uint64_t flags = spin_lock_irqsave(&sched_lock);
            next = ready_dequeue_locked();
            idle = cpu_local_this()->idle;
            if (!next && idle && idle != cpu_current()) {
                next = idle;
            }
            if (!next) {
                spin_unlock_irqrestore(&sched_lock, flags);
                __asm__ volatile("hlt");
                continue;
            }
            switch_to(next, flags);
            irq_enable();
        }
    }
}

static volatile uint32_t preempt_count_a;
static volatile uint32_t preempt_count_b;
static volatile int preempt_done;

static void preempt_worker_a(void)
{
    irq_enable();
    while (!preempt_done) {
        preempt_count_a++;
        if ((preempt_count_a & 0xfffU) == 0) {
            scheduler_yield();
            irq_enable();
        }
    }
    process_exit(0);
}

static void preempt_worker_b(void)
{
    irq_enable();
    while (!preempt_done) {
        preempt_count_b++;
        if ((preempt_count_b & 0xfffU) == 0) {
            scheduler_yield();
            irq_enable();
        }
    }
    process_exit(0);
}

int scheduler_preempt_probe(void)
{
    uint64_t start;
    process_t *a;
    process_t *b;

    preempt_count_a = 0;
    preempt_count_b = 0;
    preempt_done = 0;
    a = process_create_kernel(preempt_worker_a, "preempt-a", process_get_current_pid());
    b = process_create_kernel(preempt_worker_b, "preempt-b", process_get_current_pid());
    if (!a || !b) return -1;

    start = timer_ticks();
    while ((preempt_count_a == 0 || preempt_count_b == 0) &&
           timer_ticks() - start < 100) {
        scheduler_yield();
        irq_enable();
    }
    preempt_done = 1;
    scheduler_yield();
    irq_enable();
    process_wait_pid(a->pid);
    process_wait_pid(b->pid);

    if (preempt_count_a == 0 || preempt_count_b == 0) {
        console_puts("preempt probe FAIL\n");
        return -1;
    }
    console_puts("preempt OK\n");
    return 0;
}
