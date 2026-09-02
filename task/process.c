#include "process.h"
#include "gdt.h"
#include "heap.h"
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
static process_t *current_process;
static process_t *idle_process;
static process_t *process_registry[PROCESS_MAX];
static uint32_t next_pid = 1;

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

void scheduler_initialize(void)
{
    uint32_t index;

    ready_head = NULL;
    ready_tail = NULL;
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
    process_t *process = process_alloc(
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
    process_t *process = process_alloc(name, parent_pid, address_space);

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

    if (first == NULL)
    {
        return;
    }

    first_as = first->address_space;

    if (!address_space_is_valid(first_as))
    {
        first_as = paging_get_kernel_address_space();
        first->address_space = first_as;
    }

    if (!address_space_is_valid(first_as) || first->context.kernel_esp == 0)
    {
        return;
    }

    first->state = PROCESS_RUNNING;
    current_process = first;
    tss_set_kernel_stack(first->kernel_stack_top + PROCESS_STACK_SIZE);
    address_space_switch(first_as);
    process_switch_asm(NULL, first->context.kernel_esp, first_as->page_directory_phys);
}

static void scheduler_switch_to(process_t *next)
{
    process_t *previous;
    int previous_terminated;
    address_space_t *next_as;
    uint32_t next_cr3;

    if (next == NULL || next->state == PROCESS_TERMINATED)
    {
        next = idle_process;
    }

    if (next == NULL)
    {
        return;
    }

    if (next == current_process)
    {
        current_process->time_slice = PROCESS_TIME_SLICE;
        return;
    }

    previous = current_process;
    previous_terminated =
        (previous != NULL && previous->state == PROCESS_TERMINATED);

    if (previous != NULL && !previous_terminated)
    {
        if (previous->state != PROCESS_BLOCKED)
        {
            previous->state = PROCESS_READY;

            if (previous != idle_process)
            {
                process_append_ready(previous);
            }
        }
    }

    next_as = next->address_space;

    if (!address_space_is_valid(next_as))
    {
        /* PCB may be corrupted; do not resume it with a patched CR3. */
        next = idle_process;
        next_as = paging_get_kernel_address_space();

        if (next != NULL)
        {
            next->address_space = next_as;
        }
    }

    if (next == NULL ||
        !address_space_is_valid(next_as) ||
        next->context.kernel_esp == 0)
    {
        return;
    }

    next_cr3 = next_as->page_directory_phys;
    next->state = PROCESS_RUNNING;
    current_process = next;
    next->time_slice = PROCESS_TIME_SLICE;
    tss_set_kernel_stack(next->kernel_stack_top + PROCESS_STACK_SIZE);

    process_switch_asm(
        previous != NULL ? &previous->context.kernel_esp : NULL,
        next->context.kernel_esp,
        next_cr3
    );
}

void scheduler_tick(void)
{
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
            process->state = PROCESS_READY;
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
    current_process->state = PROCESS_BLOCKED;
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

    if (process->state == PROCESS_TERMINATED)
    {
        return 0;
    }

    process->exit_status = -1;
    process->state = PROCESS_TERMINATED;
    process_remove_from_ready(process);

    if (process == current_process)
    {
        scheduler_yield();
    }

    return 0;
}

void process_exit(int32_t status)
{
    if (current_process == NULL || current_process == idle_process)
    {
        return;
    }

    current_process->exit_status = status;
    current_process->state = PROCESS_TERMINATED;
    process_remove_from_ready(current_process);
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

    current_process->exit_status = -1;
    current_process->state = PROCESS_TERMINATED;
    process_remove_from_ready(current_process);
}

void exception_reschedule(void)
{
    process_t *next = process_pop_ready();
    address_space_t *next_as;
    uint32_t next_cr3;

    if (next == NULL ||
        next->state == PROCESS_TERMINATED ||
        next->context.kernel_esp == 0)
    {
        next = idle_process;
    }

    next_as = (next != NULL) ? next->address_space : NULL;

    if (!address_space_is_valid(next_as))
    {
        /* Smashed PCB — fall back to idle rather than halt the machine. */
        next = idle_process;
        next_as = paging_get_kernel_address_space();

        if (next != NULL)
        {
            next->address_space = next_as;
        }
    }

    if (next == NULL ||
        !address_space_is_valid(next_as) ||
        next->context.kernel_esp == 0)
    {
        for (;;)
        {
            __asm__ volatile ("cli; hlt");
        }
    }

    next_cr3 = next_as->page_directory_phys;
    next->state = PROCESS_RUNNING;
    current_process = next;
    next->time_slice = PROCESS_TIME_SLICE;
    tss_set_kernel_stack(next->kernel_stack_top + PROCESS_STACK_SIZE);

    /* previous == NULL: abandon the broken exception frame entirely. */
    process_switch_asm(NULL, next->context.kernel_esp, next_cr3);
}
