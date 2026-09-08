#include "syscall.h"
#include "syscall_abi.h"
#include "uaccess.h"
#include "fs.h"
#include "process.h"
#include "terminal.h"
#include "heap.h"

typedef int32_t (*syscall_fn_t)(const syscall_args_t *args);

static int32_t sys_exit(const syscall_args_t *args)
{
    process_exit((int32_t)args->arg1);
    return 0;
}

static int32_t sys_write(const syscall_args_t *args)
{
    char buffer[SYSCALL_MAX_WRITE + 1U];
    uint32_t length = args->arg2;
    int error;

    if (length > SYSCALL_MAX_WRITE)
    {
        length = SYSCALL_MAX_WRITE;
    }

    error = copy_from_user(buffer, args->arg1, length);

    if (error != 0)
    {
        return (int32_t)error;
    }

    buffer[length] = '\0';
    terminal_write(buffer);
    return (int32_t)length;
}

static int32_t sys_read(const syscall_args_t *args)
{
    char name[FS_MAX_PATH];
    char *file_buffer;
    uint32_t bytes_read = 0;
    int name_length;
    int error;
    int32_t result;

    name_length = str_from_user(name, args->arg1, sizeof(name));

    if (name_length < 0)
    {
        return (int32_t)name_length;
    }

    file_buffer = (char *)kmalloc(FS_MAX_FILE_SIZE);

    if (file_buffer == NULL)
    {
        return -SYSCALL_ERR_INVAL;
    }

    if (!fs_read(name, file_buffer, FS_MAX_FILE_SIZE, &bytes_read))
    {
        kfree(file_buffer);
        return -SYSCALL_ERR_NOENT;
    }

    if (args->arg2 < bytes_read)
    {
        bytes_read = args->arg2;
    }

    error = copy_to_user(args->arg3, file_buffer, bytes_read);
    kfree(file_buffer);

    if (error != 0)
    {
        return (int32_t)error;
    }

    result = (int32_t)bytes_read;
    return result;
}

static int32_t sys_open(const syscall_args_t *args)
{
    char name[FS_MAX_PATH];
    int name_length;

    name_length = str_from_user(name, args->arg1, sizeof(name));

    if (name_length < 0)
    {
        return (int32_t)name_length;
    }

    return fs_exists(name) ? 1 : 0;
}

static int32_t sys_close(const syscall_args_t *args)
{
    (void)args;
    return 0;
}

static int32_t sys_yield(const syscall_args_t *args)
{
    (void)args;
    scheduler_yield();
    return 0;
}

static int32_t sys_getpid(const syscall_args_t *args)
{
    (void)args;
    return (int32_t)process_get_current_pid();
}

static int32_t sys_abi_version(const syscall_args_t *args)
{
    (void)args;
    return (int32_t)SYSCALL_ABI_VERSION;
}

static int32_t sys_sleep(const syscall_args_t *args)
{
    process_sleep(args->arg1);
    return 0;
}

static const syscall_fn_t syscall_table[SYSCALL_MAX + 1] =
{
    [SYS_EXIT] = sys_exit,
    [SYS_WRITE] = sys_write,
    [SYS_READ] = sys_read,
    [SYS_OPEN] = sys_open,
    [SYS_CLOSE] = sys_close,
    [SYS_YIELD] = sys_yield,
    [SYS_GETPID] = sys_getpid,
    [SYS_ABI_VERSION] = sys_abi_version,
    [SYS_SLEEP] = sys_sleep
};

int32_t syscall_handler(const syscall_args_t *args)
{
    process_t *process;
    syscall_fn_t handler;

    if (args == NULL)
    {
        return -SYSCALL_ERR_INVAL;
    }

    process = process_get_current();

    if (process == NULL || !process->is_user)
    {
        return -SYSCALL_ERR_PERM;
    }

    if (args->number == 0 || args->number > SYSCALL_MAX)
    {
        return -SYSCALL_ERR_NOSYS;
    }

    handler = syscall_table[args->number];

    if (handler == NULL)
    {
        return -SYSCALL_ERR_NOSYS;
    }

    return handler(args);
}

void syscall_initialize(void)
{
}
