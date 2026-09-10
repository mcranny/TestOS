#ifndef TESTOS_UEFI_TASK_PROCESS_H
#define TESTOS_UEFI_TASK_PROCESS_H

#include "types.h"
#include "mm/paging.h"

#define PROCESS_NAME_MAX 32
#define PROCESS_TIME_SLICE 5
#define PROCESS_MAX 32
#define PROCESS_KERNEL_STACK_SIZE 8192

typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_SLEEPING,
    PROC_ZOMBIE
} process_state_t;

typedef struct process {
    uint32_t pid;
    process_state_t state;
    char name[PROCESS_NAME_MAX];
    uint64_t kernel_rsp;
    uint64_t cr3;
    uint32_t time_slice;
    uint32_t parent_pid;
    uint64_t wake_tick;
    int32_t exit_code;
    address_space_t *as;
    uint8_t *kernel_stack;
    int is_user;
    int protected; /* shell/idle: not killable */
    uint64_t user_entry;
    uint64_t user_stack;
    /* In-flight SYSCALL user RSP; CPU live slot is current_syscall_user_rsp. */
    uint64_t syscall_user_rsp;
    struct process *next;
} process_t;

typedef void (*process_entry_t)(void);

void scheduler_init(void);
void scheduler_ap_online(uint32_t cpu);
void scheduler_ap_idle(void) __attribute__((noreturn));
void scheduler_tick(void);
void scheduler_yield(void);
void scheduler_on_irq_exit(void);
void scheduler_enable_preempt(void);
void scheduler_kick_others(void);
/* Thin wrappers over cpu_current() — preferred public API for most callers. */
process_t *process_get_current(void);
uint32_t process_get_current_pid(void);
process_t *process_find_by_pid(uint32_t pid);
uint32_t process_table_count(void);
process_t *process_at(uint32_t index);
process_t *process_create_kernel(process_entry_t entry, const char *name, uint32_t parent_pid);
process_t *process_create_user(uint64_t entry, uint64_t user_stack, address_space_t *as,
                               const char *name, uint32_t parent_pid);
void process_exit(int32_t code);
int process_terminate(uint32_t pid);
void process_sleep_ticks(uint64_t ticks);
void process_reap_zombies(void);
void process_wait_pid(uint32_t pid);
int scheduler_preempt_probe(void);

void process_switch(process_t *prev, process_t *next);
void process_enter_user(uint64_t entry, uint64_t user_stack);

#endif
