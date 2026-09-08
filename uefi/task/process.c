#include "task/process.h"
#include "user/syscall.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "arch/tss.h"
#include "arch/io.h"
#include "drivers/console.h"
#include "lib/string.h"
#include "platform.h"

static process_t table[PROCESS_MAX];
static process_t *current;
static process_t *ready_head;
static process_t *ready_tail;
static uint32_t next_pid = 1;
static int scheduler_ready;
static volatile int need_resched;

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

static void ready_enqueue(process_t *p)
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

static process_t *ready_dequeue(void)
{
    process_t *p = ready_head;
    if (!p) return NULL;
    ready_head = p->next;
    if (!ready_head) ready_tail = NULL;
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
    if (!p || !p->kernel_stack) return;
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
    /* Interrupt/privilege stack switches require a 16-byte-aligned RSP0. */
    top = ((uint64_t)p->kernel_stack + PROCESS_KERNEL_STACK_SIZE + 15) & ~15ULL;
    stack = (uint64_t *)top;
    /* Fake saved frame for process_switch pop order: r15..rbp then ret */
    *--stack = (uint64_t)entry;              /* return address */
    *--stack = 0; /* rbp */
    *--stack = 0; /* rbx */
    *--stack = 0; /* r12 */
    *--stack = 0; /* r13 */
    *--stack = 0; /* r14 */
    *--stack = 0; /* r15 */
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
        process_reap_zombies();
        __asm__ volatile("hlt");
        scheduler_yield();
    }
}

static void worker_a(void)
{
    console_puts("scheduler worker A\n");
    process_exit(0);
}

static void worker_b(void)
{
    console_puts("scheduler worker B\n");
    process_exit(0);
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
    /* Shell needs a real RSP0 so IRQs after user exits do not keep a dead stack. */
    shell->kernel_stack = (uint8_t *)kmalloc(PROCESS_KERNEL_STACK_SIZE + 16);
    if (!shell->kernel_stack) {
        panic("scheduler: shell stack alloc failed");
    }
    current = shell;
    set_kernel_stacks(shell);

    idle = process_create_kernel(idle_thread, "idle", shell->pid);
    if (idle) {
        idle->protected = 1;
    }
    (void)process_create_kernel(worker_a, "worker-a", shell->pid);
    (void)process_create_kernel(worker_b, "worker-b", shell->pid);
    (void)idle;

    scheduler_ready = 1;
    console_puts("scheduler OK\n");
}

static void switch_to(process_t *next)
{
    process_t *prev = current;
    if (!next || next == prev) return;
    if (prev->state == PROC_RUNNING) {
        prev->state = PROC_READY;
        ready_enqueue(prev);
    }
    next->state = PROC_RUNNING;
    next->time_slice = PROCESS_TIME_SLICE;
    current = next;
    next->state = PROC_RUNNING;
    next->time_slice = PROCESS_TIME_SLICE;
    set_kernel_stacks(next);
    process_switch(prev, next);
}

void scheduler_tick(void)
{
    process_t *p;

    if (!scheduler_ready || !current) return;

    /* Wake sleepers */
    for (p = &table[0]; p < &table[PROCESS_MAX]; p++) {
        if (p->state == PROC_SLEEPING && timer_ticks() >= p->wake_tick) {
            ready_enqueue(p);
            need_resched = 1;
        }
    }

    if (current->time_slice > 0) {
        current->time_slice--;
    }
    if (current->time_slice == 0) {
        if (ready_head != NULL) {
            need_resched = 1;
        } else {
            current->time_slice = PROCESS_TIME_SLICE;
        }
    }
}

void scheduler_yield(void)
{
    process_t *next;
    if (!scheduler_ready) return;
    next = ready_dequeue();
    if (!next) {
        if (current) {
            current->time_slice = PROCESS_TIME_SLICE;
        }
        need_resched = 0;
        return;
    }
    need_resched = 0;
    switch_to(next);
}

void scheduler_on_irq_exit(void)
{
    if (!need_resched) return;
    /*
     * Called after PIC EOI on the timer IRQ return path (IF still clear from
     * the interrupt gate). Re-enable interrupts so the next task is not stuck
     * in hlt with IRQs masked, switch, then CLI again before iretq.
     */
    __asm__ volatile("sti" ::: "memory");
    scheduler_yield();
    __asm__ volatile("cli" ::: "memory");
}

process_t *process_get_current(void)
{
    return current;
}

uint32_t process_get_current_pid(void)
{
    return current ? current->pid : 0;
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
    process_t *p = alloc_slot();
    if (!p) return NULL;
    p->pid = next_pid++;
    p->parent_pid = parent_pid;
    p->as = address_space_kernel();
    p->cr3 = p->as->pml4_phys;
    p->is_user = 0;
    p->time_slice = PROCESS_TIME_SLICE;
    name_copy(p->name, name);
    prepare_kernel_stack(p, entry);
    ready_enqueue(p);
    return p;
}

process_t *process_create_user(uint64_t entry, uint64_t user_stack, address_space_t *as,
                               const char *name, uint32_t parent_pid)
{
    process_t *p = alloc_slot();
    if (!p || !as) return NULL;
    p->pid = next_pid++;
    p->parent_pid = parent_pid;
    p->as = as;
    p->cr3 = as->pml4_phys;
    p->is_user = 1;
    p->user_entry = entry;
    p->user_stack = user_stack;
    p->time_slice = PROCESS_TIME_SLICE;
    name_copy(p->name, name);
    prepare_kernel_stack(p, user_trampoline);
    ready_enqueue(p);
    return p;
}

void process_exit(int32_t code)
{
    process_t *p = current;
    process_t *next;
    process_t *candidate;
    process_t **pp;
    process_t *prev_link;
    if (!p) return;
    p->exit_code = code;
    p->state = PROC_ZOMBIE;

    /* Prefer a non-idle ready task (e.g. waiting shell) over idle. */
    next = NULL;
    pp = &ready_head;
    prev_link = NULL;
    while (*pp) {
        candidate = *pp;
        /* Skip protected idle; prefer shell/user waiters. */
        if (!(candidate->protected && candidate->name[0] == 'i')) {
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
        next = ready_dequeue();
    }
    if (!next) {
        for (;;) __asm__ volatile("hlt");
    }
    current = next;
    next->state = PROC_RUNNING;
    next->time_slice = PROCESS_TIME_SLICE;
    set_kernel_stacks(next);
    /* SYS_EXIT arrives with IF clear (FMASK); next task must not inherit that. */
    __asm__ volatile("sti" ::: "memory");
    process_switch(NULL, next);
}

int process_terminate(uint32_t pid)
{
    process_t *p = process_find_by_pid(pid);
    if (!p || p->pid == 0) return -1;
    if (p->protected) return -1;
    if (p == current) {
        process_exit(137);
        return 0;
    }
    if (p->state == PROC_UNUSED || p->state == PROC_ZOMBIE) return -1;
    /* Remove from ready queue if present */
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
    return 0;
}

void process_sleep_ticks(uint64_t ticks)
{
    if (!current) return;
    current->wake_tick = timer_ticks() + ticks;
    current->state = PROC_SLEEPING;
    {
        process_t *next = ready_dequeue();
        if (!next) {
            current->state = PROC_RUNNING;
            return;
        }
        switch_to(next);
    }
}

void process_reap_zombies(void)
{
    uint32_t i;
    for (i = 0; i < PROCESS_MAX; i++) {
        process_t *p = &table[i];
        if (p->state != PROC_ZOMBIE) continue;
        if (p->is_user && p->as && p->as != address_space_kernel()) {
            address_space_destroy(p->as);
            p->as = NULL;
        }
        if (p->kernel_stack) {
            kfree(p->kernel_stack);
            p->kernel_stack = NULL;
        }
        p->state = PROC_UNUSED;
    }
}

void process_wait_pid(uint32_t pid)
{
    for (;;) {
        process_t *p = process_find_by_pid(pid);
        process_t *next;
        process_t **pp;
        process_t *prev_link;

        if (!p || p->state == PROC_ZOMBIE || p->state == PROC_UNUSED) {
            process_reap_zombies();
            return;
        }

        /* Prefer the waited task so we do not stall behind idle's hlt. */
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
            switch_to(p);
            continue;
        }

        next = ready_dequeue();
        if (!next) {
            __asm__ volatile("hlt");
            continue;
        }
        switch_to(next);
    }
}

/* Busy-loop workers: both must make progress without cooperative yield. */
static volatile uint32_t preempt_count_a;
static volatile uint32_t preempt_count_b;
static volatile int preempt_done;

static void preempt_worker_a(void)
{
    while (!preempt_done) {
        preempt_count_a++;
    }
    process_exit(0);
}

static void preempt_worker_b(void)
{
    while (!preempt_done) {
        preempt_count_b++;
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
           timer_ticks() - start < 50) {
        /* Spin; timer preemption must schedule the workers. */
        __asm__ volatile("pause");
    }
    preempt_done = 1;
    process_wait_pid(a->pid);
    process_wait_pid(b->pid);

    if (preempt_count_a == 0 || preempt_count_b == 0) {
        console_puts("preempt probe FAIL\n");
        return -1;
    }
    console_puts("preempt OK\n");
    return 0;
}
