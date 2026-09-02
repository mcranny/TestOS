#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#include "paging.h"

#define PROCESS_STACK_SIZE 32768
#define PROCESS_TIME_SLICE 5
#define PROCESS_MAX          16

#define PROCESS_EXIT_NONE 0x7FFFFFFF

typedef enum
{
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
} process_state_t;

typedef struct process_context
{
    uint32_t kernel_esp;
} process_context_t;

typedef void (*process_entry_t)(void);

typedef struct process
{
    uint32_t pid;
    uint32_t parent_pid;
    process_state_t state;
    process_context_t context;
    address_space_t *address_space;
    uint32_t kernel_stack_top;
    uint32_t user_entry;
    uint32_t user_stack_top;
    int is_user;
    int32_t exit_status;
    char name[16];
    uint32_t time_slice;
    uint32_t wake_tick;
    struct process *next;
} process_t;

void scheduler_initialize(void);
void scheduler_start(void);
void scheduler_tick(void);
void scheduler_yield(void);
void scheduler_wake_sleepers(void);
void process_sleep(uint32_t ticks);

process_t *process_create_kernel(
    process_entry_t entry,
    const char *name,
    uint32_t parent_pid
);
process_t *process_create_user(
    uint32_t entry,
    uint32_t user_stack,
    const char *name,
    uint32_t parent_pid,
    address_space_t *address_space
);

void process_exit(int32_t status);
void process_handle_exception(void *frame);
void exception_reschedule(void);

process_t *process_get_current(void);
uint32_t process_get_current_pid(void);
process_t *process_find_by_pid(uint32_t pid);
int process_terminate(uint32_t pid);
void process_list(void);

#endif
