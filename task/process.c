#include "process.h"
#include "config.h"
#include "gdt.h"
#include "heap.h"
#include "log.h"
#include "memory.h"
#include "paging.h"
#include "terminal.h"
#include "timer.h"
#include "tss.h"

extern void process_switch_asm(
    uint32_t *current_esp,
    uint32_t next_esp,
    uint32_t next_cr3
);
extern void process_enter_user_mode(uint32_t eip, uint32_t user_stack);

static process_t *ready_head;
static process_t *ready_tail;
static process_t *terminated_head;
static process_t *current_process;
static process_t *idle_process;
static process_t *process_registry[PROCESS_MAX];
static uint32_t next_pid = 1;

static int process_is_registered(process_t *process)
{
    uint32_t index;

    if (process == NULL)
    {
        return 0;
    }

    for (index = 0; index < PROCESS_MAX; index++)
    {
        if (process_registry[index] == process)
        {
            return 1;
        }
    }

    return 0;
}

static int process_transition_allowed(
    process_state_t old_state,
    process_state_t new_state
)
{
    if (old_state == new_state)
    {
        return 1;
    }

    if (new_state == PROCESS_TERMINATED)
    {
        return old_state != PROCESS_TERMINATED;
    }

    if (old_state == PROCESS_TERMINATED)
    {
        return 0;
    }

    switch (old_state)
    {
        case PROCESS_READY:
            return new_state == PROCESS_RUNNING;
        case PROCESS_RUNNING:
            return new_state == PROCESS_READY || new_state == PROCESS_BLOCKED;
        case PROCESS_BLOCKED:
            return new_state == PROCESS_READY;
        default:
            return 0;
    }
}

static void process_set_state(process_t *process, process_state_t new_state)
{
    if (process == NULL)
    {
        return;
    }

    if (!process_transition_allowed(process->state, new_state))
    {
        klog(KLOG_ERROR, "PROC", "Invalid process state transition");
        klog_uint(KLOG_ERROR, "PROC", "pid=", process->pid);
#if TESTOS_DEBUG
        panic("PROC", "Invalid process state transition");
#else
        return;
#endif
    }

    process->state = new_state;
}

static void process_register(process_t *process)
{
    uint32_t index;

    for (index = 0; index < PROCESS_MAX; index++)
    {
        if (process_registry[index] == NULL)
        {
            process_registry[index] = process;
            return;
        }
    }
}

static void process_unregister(process_t *process)
{
    uint32_t index;

    for (index = 0; index < PROCESS_MAX; index++)
    {
        if (process_registry[index] == process)
        {
            process_registry[index] = NULL;
            return;
        }
    }
}

static void process_remove_from_ready(process_t *target)
{
    process_t *process = ready_head;
    process_t *previous = NULL;

    while (process != NULL)
    {
        if (process == target)
        {
            if (previous == NULL)
            {
                ready_head = process->next;
            }
            else
            {
                previous->next = process->next;
            }

            if (ready_tail == target)
            {
                ready_tail = previous;
            }

            process->next = NULL;
            return;
        }

        previous = process;
        process = process->next;
    }
}

static void process_enqueue_terminated(process_t *process)
{
    if (process == NULL || process == idle_process)
    {
        return;
    }

    process->reap_next = terminated_head;
    terminated_head = process;
}

static int process_esp_in_stack(process_t *process)
{
    uint32_t stack_low;
    uint32_t stack_high;

    if (process == NULL || process->kernel_stack_top == 0)
    {
        return 0;
    }

    stack_low = process->kernel_stack_top + sizeof(uint32_t);
    stack_high = process->kernel_stack_top + PROCESS_STACK_SIZE;

    return process->context.kernel_esp >= stack_low &&
           process->context.kernel_esp <= stack_high;
}

int process_check_stack_canary(process_t *process)
{
    uint32_t canary;

    if (process == NULL || process->kernel_stack_top == 0)
    {
        return 0;
    }

    canary = *(volatile uint32_t *)process->kernel_stack_top;
    return canary == PROCESS_STACK_CANARY;
}

static void process_verify_stack_or_panic(process_t *process, const char *when)
{
    (void)when;

    if (process == NULL || process->kernel_stack_top == 0)
    {
        return;
    }

    if (!process_check_stack_canary(process))
    {
        klog(KLOG_PANIC, "PROC", "KERNEL STACK CORRUPTION");
        klog_uint(KLOG_PANIC, "PROC", "PID: ", process->pid);
        klog(KLOG_PANIC, "PROC", process->name);
        panic("PROC", "KERNEL STACK CORRUPTION");
    }
}

static int process_is_schedulable(process_t *process)
{
    if (process == NULL)
    {
        return 0;
    }

    if (!process_is_registered(process))
    {
        klog(KLOG_ERROR, "PROC", "Process pointer not in registry");
        return 0;
    }

    if (process->state == PROCESS_TERMINATED)
    {
        return 0;
    }

    if (!address_space_is_valid(process->address_space))
    {
        klog(KLOG_ERROR, "PROC", "Invalid address space");
        return 0;
    }

    if (process->kernel_stack_top == 0 || process->context.kernel_esp == 0)
    {
        klog(KLOG_ERROR, "PROC", "Invalid kernel stack");
        return 0;
    }

    if (!process_esp_in_stack(process))
    {
        klog(KLOG_ERROR, "PROC", "Kernel ESP outside stack");
        return 0;
    }

    if (!process_check_stack_canary(process))
    {
        process_verify_stack_or_panic(process, "schedulable");
        return 0;
    }

    return 1;
}

static void process_remove_from_terminated(process_t *target)
{
    process_t *process = terminated_head;
    process_t *previous = NULL;

    while (process != NULL)
    {
        if (process == target)
        {
            if (previous == NULL)
            {
                terminated_head = process->reap_next;
            }
            else
            {
                previous->reap_next = process->reap_next;
            }

            process->reap_next = NULL;
            return;
        }

        previous = process;
        process = process->reap_next;
    }
}

/*
 * Free all resources owned by a terminated process. Must not be current
 * (may free the kernel stack we would still be running on).
 */
static void process_free(process_t *process)
{
    address_space_t *kernel_as = paging_get_kernel_address_space();

    if (process == NULL || process == idle_process || process == current_process)
    {
        return;
    }

    process_remove_from_ready(process);
    process_remove_from_terminated(process);
    process_unregister(process);

    if (process->is_user &&
        process->address_space != NULL &&
        process->address_space != kernel_as)
    {
        address_space_destroy(process->address_space);
        process->address_space = NULL;
    }

    if (process->kernel_stack_top != 0)
    {
        kfree((void *)process->kernel_stack_top);
        process->kernel_stack_top = 0;
        process->context.kernel_esp = 0;
    }

    process->reap_next = NULL;
    kfree(process);
}

void process_reap_terminated(void)
{
    process_t *process;
    process_t *next;

    if (kernel_is_panicked())
    {
        return;
    }

    process = terminated_head;
    terminated_head = NULL;

    while (process != NULL)
    {
        next = process->reap_next;
        process->reap_next = NULL;

        if (process != current_process &&
            process != idle_process &&
            process->state == PROCESS_TERMINATED)
        {
            process_free(process);
        }
        else if (process->state == PROCESS_TERMINATED)
        {
            process->reap_next = terminated_head;
            terminated_head = process;
        }

        process = next;
    }
}

static const char *process_state_name(process_state_t state)
{
    switch (state)
    {
        case PROCESS_READY:
            return "ready";
        case PROCESS_RUNNING:
            return "running";
        case PROCESS_BLOCKED:
            return "blocked";
        case PROCESS_TERMINATED:
            return "terminated";
        default:
            return "unknown";
    }
}

static void user_process_trampoline(void)
{
    if (current_process == NULL)
    {
        for (;;)
        {
            __asm__ volatile ("hlt");
        }
    }

    process_enter_user_mode(
        current_process->user_entry,
        current_process->user_stack_top
    );
}

static void idle_process_entry(void)
{
    for (;;)
    {
        process_reap_terminated();
        __asm__ volatile ("hlt");
    }
}

static void process_append_ready(process_t *process)
{
    process->next = NULL;

    if (ready_tail == NULL)
    {
        ready_head = process;
        ready_tail = process;
        return;
    }

    ready_tail->next = process;
    ready_tail = process;
}

static process_t *process_pop_ready(void)
{
    while (ready_head != NULL && ready_head->state == PROCESS_TERMINATED)
    {
        process_t *terminated = ready_head;

        ready_head = terminated->next;
        terminated->next = NULL;
    }

    if (ready_head == NULL)
    {
        ready_tail = NULL;
        return idle_process;
    }

    process_t *process = ready_head;

    ready_head = process->next;

    if (ready_head == NULL)
    {
        ready_tail = NULL;
    }

    process->next = NULL;
    return process;
}

static void process_prepare_kernel_stack(process_t *process, process_entry_t entry)
{
    uint32_t *stack;

    process->kernel_stack_top = (uint32_t)kmalloc(PROCESS_STACK_SIZE);

    if (process->kernel_stack_top == 0)
    {
        return;
    }

    *(uint32_t *)process->kernel_stack_top = PROCESS_STACK_CANARY;

    stack = (uint32_t *)(process->kernel_stack_top + PROCESS_STACK_SIZE);
    *--stack = (uint32_t)entry;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0x202;
    process->context.kernel_esp = (uint32_t)stack;
}

static process_t *process_alloc(
    const char *name,
    uint32_t parent_pid,
    address_space_t *address_space
)
{
    process_t *process = (process_t *)kmalloc(sizeof(process_t));
    int index = 0;

    if (process == NULL)
    {
        return NULL;
    }

    process->pid = next_pid++;
    process->parent_pid = parent_pid;
    process->state = PROCESS_READY;
    process->address_space = address_space;
    process->is_user = 0;
    process->time_slice = PROCESS_TIME_SLICE;
    process->next = NULL;
    process->reap_next = NULL;
    process->user_entry = 0;
    process->user_stack_top = 0;
    process->exit_status = PROCESS_EXIT_NONE;
    process->kernel_stack_top = 0;
    process->context.kernel_esp = 0;
    process->wake_tick = 0;

    while (name[index] != '\0' && index < 15)
    {
        process->name[index] = name[index];
        index++;
    }

    process->name[index] = '\0';
    process_register(process);
    return process;
}

static void process_mark_terminated(process_t *process, int32_t status)
{
    if (process == NULL || process == idle_process)
    {
        return;
    }

    if (process->state == PROCESS_TERMINATED)
    {
        return;
    }

    process->exit_status = status;
    process_set_state(process, PROCESS_TERMINATED);
    process_remove_from_ready(process);
    process_enqueue_terminated(process);
}

void scheduler_initialize(void)
{
    uint32_t index;

    ready_head = NULL;
    ready_tail = NULL;
    terminated_head = NULL;
    current_process = NULL;

    for (index = 0; index < PROCESS_MAX; index++)
    {
        process_registry[index] = NULL;
    }

    idle_process = process_alloc("idle", 0, paging_get_kernel_address_space());

    if (idle_process == NULL)
    {
        return;
    }

    idle_process->pid = 0;
    next_pid = 1;
    process_registry[0] = idle_process;
    process_prepare_kernel_stack(idle_process, idle_process_entry);
}

process_t *process_create_kernel(
    process_entry_t entry,
    const char *name,
    uint32_t parent_pid
)
{
    process_t *process;

    process_reap_terminated();

    process = process_alloc(
        name,
        parent_pid,
        paging_get_kernel_address_space()
    );

    if (process == NULL)
    {
        return NULL;
    }

    process_prepare_kernel_stack(process, entry);

    if (process->context.kernel_esp == 0)
    {
        process_unregister(process);
        kfree(process);
        return NULL;
    }

    process_append_ready(process);
    return process;
}

process_t *process_create_user(
    uint32_t entry,
    uint32_t user_stack,
    const char *name,
    uint32_t parent_pid,
    address_space_t *address_space
)
{
    process_t *process;

    process_reap_terminated();

    process = process_alloc(name, parent_pid, address_space);

    if (process == NULL)
    {
        return NULL;
    }

    process->is_user = 1;
    process->user_entry = entry;
    process->user_stack_top = user_stack;
    process_prepare_kernel_stack(process, user_process_trampoline);

    if (process->context.kernel_esp == 0)
    {
        process_unregister(process);
        kfree(process);
        return NULL;
    }

    process_append_ready(process);
    return process;
}

void scheduler_start(void)
{
    process_t *first = process_pop_ready();
    address_space_t *first_as;

    if (kernel_is_panicked())
    {
        return;
    }

    if (first == NULL || !process_is_schedulable(first))
    {
        first = idle_process;
    }

    if (first == NULL || !process_is_schedulable(first))
    {
        panic("SCHED", "No runnable process at start");
    }

    first_as = first->address_space;
    process_set_state(first, PROCESS_RUNNING);
    current_process = first;
    tss_set_kernel_stack(first->kernel_stack_top + PROCESS_STACK_SIZE);
    address_space_switch(first_as);
    process_switch_asm(NULL, first->context.kernel_esp, first_as->page_directory_phys);
}

static void scheduler_switch_to(process_t *next)
{
    process_t *previous;
    process_t *outgoing;
    int previous_terminated;
    address_space_t *next_as;
    uint32_t next_cr3;

    if (kernel_is_panicked())
    {
        return;
    }

    if (next == NULL || next->state == PROCESS_TERMINATED ||
        !process_is_schedulable(next))
    {
        next = idle_process;
    }

    if (next == NULL || !process_is_schedulable(next))
    {
        panic("SCHED", "No runnable process");
    }

    if (next == current_process)
    {
        current_process->time_slice = PROCESS_TIME_SLICE;
        return;
    }

    previous = current_process;
    outgoing = previous;
    previous_terminated =
        (previous != NULL && previous->state == PROCESS_TERMINATED);

    if (previous != NULL)
    {
        process_verify_stack_or_panic(previous, "switch-from");
    }

    process_verify_stack_or_panic(next, "switch-to");

    if (previous != NULL && !previous_terminated)
    {
        if (previous->state != PROCESS_BLOCKED)
        {
            process_set_state(previous, PROCESS_READY);

            if (previous != idle_process)
            {
                process_append_ready(previous);
            }
        }
    }

    next_as = next->address_space;
    next_cr3 = next_as->page_directory_phys;

    /*
     * Keep IRQs off across the handoff so a tick cannot reap `outgoing`
     * while we still execute on its kernel stack.
     */
    __asm__ volatile ("cli");

    process_set_state(next, PROCESS_RUNNING);
    current_process = next;
    next->time_slice = PROCESS_TIME_SLICE;
    tss_set_kernel_stack(next->kernel_stack_top + PROCESS_STACK_SIZE);

    process_switch_asm(
        previous != NULL ? &previous->context.kernel_esp : NULL,
        next->context.kernel_esp,
        next_cr3
    );

    __asm__ volatile ("sti");

    if (outgoing != NULL &&
        outgoing != current_process &&
        outgoing->state == PROCESS_TERMINATED)
    {
        process_free(outgoing);
    }

    process_reap_terminated();
}

void scheduler_tick(void)
{
    if (kernel_is_panicked())
    {
        return;
    }

    process_reap_terminated();

    if (current_process == NULL)
    {
        return;
    }

    if (current_process->time_slice > 0)
    {
        current_process->time_slice--;
    }

    if (current_process->time_slice > 0)
    {
        return;
    }

    scheduler_switch_to(process_pop_ready());
}

void scheduler_yield(void)
{
    if (kernel_is_panicked())
    {
        return;
    }

    process_reap_terminated();
    scheduler_switch_to(process_pop_ready());
}

void scheduler_wake_sleepers(void)
{
    uint32_t now = timer_get_ticks();
    uint32_t index;

    for (index = 0; index < PROCESS_MAX; index++)
    {
        process_t *process = process_registry[index];

        if (process == NULL ||
            process->state != PROCESS_BLOCKED ||
            process->wake_tick == 0)
        {
            continue;
        }

        if (now >= process->wake_tick)
        {
            process->wake_tick = 0;
            process_set_state(process, PROCESS_READY);
            process_append_ready(process);
        }
    }
}

void process_sleep(uint32_t ticks)
{
    if (current_process == NULL ||
        current_process == idle_process ||
        ticks == 0)
    {
        return;
    }

    current_process->wake_tick = timer_get_ticks() + ticks;
    process_set_state(current_process, PROCESS_BLOCKED);
    process_remove_from_ready(current_process);
    scheduler_yield();
}

process_t *process_find_by_pid(uint32_t pid)
{
    uint32_t index;

    for (index = 0; index < PROCESS_MAX; index++)
    {
        if (process_registry[index] != NULL &&
            process_registry[index]->pid == pid)
        {
            return process_registry[index];
        }
    }

    return NULL;
}

int process_terminate(uint32_t pid)
{
    process_t *process = process_find_by_pid(pid);

    if (process == NULL || process == idle_process)
    {
        return -1;
    }

    /* Idle, shell, and other kernel threads are not killable. */
    if (!process->is_user)
    {
        return -1;
    }

    if (process->state == PROCESS_TERMINATED)
    {
        return 0;
    }

    process_mark_terminated(process, -1);

    if (process == current_process)
    {
        scheduler_yield();
        return 0;
    }

    process_reap_terminated();
    return 0;
}

void process_exit(int32_t status)
{
    if (current_process == NULL || current_process == idle_process)
    {
        return;
    }

    process_mark_terminated(current_process, status);
    scheduler_yield();
}

process_t *process_get_current(void)
{
    return current_process;
}

uint32_t process_get_current_pid(void)
{
    if (current_process == NULL)
    {
        return 0;
    }

    return current_process->pid;
}

static void process_write_decimal(int32_t value)
{
    char buffer[12];
    int index = 0;
    uint32_t magnitude;
    int negative = 0;

    if (value < 0)
    {
        negative = 1;
        magnitude = (uint32_t)(-(value + 1)) + 1U;
    }
    else
    {
        magnitude = (uint32_t)value;
    }

    if (magnitude == 0)
    {
        terminal_putchar('0');
        return;
    }

    while (magnitude > 0)
    {
        buffer[index++] = (char)('0' + (magnitude % 10U));
        magnitude /= 10U;
    }

    if (negative)
    {
        terminal_putchar('-');
    }

    while (index > 0)
    {
        terminal_putchar(buffer[--index]);
    }
}

void process_list(void)
{
    uint32_t index;

    terminal_write("  PID  PPID  STATE       NAME\n");

    for (index = 0; index < PROCESS_MAX; index++)
    {
        process_t *process = process_registry[index];

        if (process == NULL || process->state == PROCESS_TERMINATED)
        {
            continue;
        }

        terminal_write("  ");
        process_write_decimal((int32_t)process->pid);
        terminal_write("    ");
        process_write_decimal((int32_t)process->parent_pid);
        terminal_write("     ");
        terminal_write(process_state_name(process->state));
        terminal_write("     ");
        terminal_write(process->name);
        terminal_putchar('\n');
    }
}

void process_handle_exception(void *frame)
{
    (void)frame;

    if (current_process == NULL || current_process == idle_process)
    {
        return;
    }

    process_mark_terminated(current_process, -1);
}

void exception_reschedule(void)
{
    process_t *dying = current_process;
    process_t *next = process_pop_ready();
    address_space_t *next_as;
    uint32_t next_cr3;

    if (kernel_is_panicked())
    {
        for (;;)
        {
            __asm__ volatile ("cli; hlt");
        }
    }

    if (next == NULL || !process_is_schedulable(next))
    {
        next = idle_process;
    }

    if (next == NULL || !process_is_schedulable(next))
    {
        panic("SCHED", "No runnable process after exception");
    }

    next_as = next->address_space;
    next_cr3 = next_as->page_directory_phys;

    __asm__ volatile ("cli");

    process_set_state(next, PROCESS_RUNNING);
    current_process = next;
    next->time_slice = PROCESS_TIME_SLICE;
    tss_set_kernel_stack(next->kernel_stack_top + PROCESS_STACK_SIZE);

    /*
     * previous == NULL: abandon the broken exception frame. The dying task
     * stays TERMINATED until the next process_reap_terminated() on the survivor.
     */
    (void)dying;
    process_switch_asm(NULL, next->context.kernel_esp, next_cr3);

    __asm__ volatile ("sti");
    process_reap_terminated();
}
