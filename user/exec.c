#include "exec.h"
#include "process.h"
#include "fs.h"
#include "paging.h"
#include "pmm.h"
#include "memory.h"
#include "heap.h"

#define EXEC_MAX_ARGS 8

#define USER_STACK_TOP \
    (USER_STACK_BASE + (USER_STACK_PAGES * PAGE_SIZE))

static void copy_process_name(const char *source, char *destination)
{
    int index = 0;

    while (source[index] != '\0' && index < 15)
    {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static int arg_length(const char *string)
{
    int length = 0;

    while (string[length] != '\0')
    {
        length++;
    }

    return length;
}

static uint32_t setup_user_stack(
    uint32_t stack_physical,
    int argc,
    const char **argv
)
{
    uint8_t *page = (uint8_t *)stack_physical;
    uint32_t string_addrs[EXEC_MAX_ARGS];
    uint32_t stack_pointer = USER_STACK_TOP;
    uint32_t stack_size = USER_STACK_PAGES * PAGE_SIZE;
    int index;

    memset(page, 0, stack_size);

    for (index = argc - 1; index >= 0; index--)
    {
        int length = arg_length(argv[index]) + 1;

        if (stack_pointer - (uint32_t)length < USER_STACK_BASE + 64U)
        {
            break;
        }

        stack_pointer -= (uint32_t)length;
        string_addrs[index] = stack_pointer;
        memcpy(
            page + (stack_pointer - USER_STACK_BASE),
            argv[index],
            (size_t)length
        );
    }

    /* Room for argc + argv pointers + NULL, then 16-byte align for crt0. */
    stack_pointer -= (uint32_t)(argc + 2) * 4U;
    stack_pointer &= ~0xFU;

    *(uint32_t *)(page + (stack_pointer - USER_STACK_BASE)) = (uint32_t)argc;

    for (index = 0; index < argc; index++)
    {
        *(uint32_t *)(
            page + (stack_pointer - USER_STACK_BASE + 4U + (uint32_t)index * 4U)
        ) = string_addrs[index];
    }

    *(uint32_t *)(
        page + (stack_pointer - USER_STACK_BASE + 4U + (uint32_t)argc * 4U)
    ) = 0;

    return stack_pointer;
}

static int load_executable(
    const char *path,
    char *buffer,
    uint32_t buffer_size,
    uint32_t *size_out
)
{
    char resolved[FS_MAX_PATH];
    uint32_t size = 0;

    if (!fs_resolve_path(path, resolved))
    {
        return 0;
    }

    if (!fs_read(resolved, buffer, buffer_size, &size) || size == 0)
    {
        return 0;
    }

    /*
     * fs_read writes a trailing '\0' for text consumers. For binaries that
     * fill the buffer that would truncate; for smaller images it only writes
     * past the payload, so keep size as the real file length.
     */
    if (size >= buffer_size)
    {
        return 0;
    }

    *size_out = size;
    return 1;
}

int process_exec(
    const char *path,
    uint32_t parent_pid,
    int argc,
    const char **argv
)
{
    char resolved[FS_MAX_PATH];
    char *buffer;
    char process_name[16];
    uint32_t size = 0;
    address_space_t *address_space;
    uint32_t page_count;
    uint32_t code_physical;
    uint32_t stack_physical;
    uint32_t user_stack_top;
    uint32_t index;
    process_t *process;
    const char *default_argv[1];
    int result = -1;

    if (!fs_resolve_path(path, resolved))
    {
        return -1;
    }

    if (!fs_is_executable(resolved))
    {
        return -1;
    }

    /* Never put FS_MAX_FILE_SIZE on the kernel stack (PROCESS_STACK_SIZE). */
    buffer = (char *)kmalloc(FS_MAX_FILE_SIZE);

    if (buffer == NULL)
    {
        return -1;
    }

    if (!load_executable(resolved, buffer, FS_MAX_FILE_SIZE, &size))
    {
        kfree(buffer);
        return -1;
    }

    if (argc <= 0)
    {
        default_argv[0] = fs_base_name(resolved);
        argv = default_argv;
        argc = 1;
    }

    if (argc > EXEC_MAX_ARGS)
    {
        argc = EXEC_MAX_ARGS;
    }

    address_space = address_space_create();

    if (address_space == NULL)
    {
        kfree(buffer);
        return -1;
    }

    page_count = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;

    /*
     * objcopy -O binary omits trailing NOBITS (.bss). Keep a spare page so a
     * small BSS that starts just after the file image is still mapped.
     */
    page_count += 1U;

    code_physical = pmm_alloc_contiguous(page_count);

    if (code_physical == 0)
    {
        address_space_destroy(address_space);
        kfree(buffer);
        return -1;
    }

    memset((void *)code_physical, 0, page_count * PAGE_SIZE);
    memcpy((void *)code_physical, buffer, size);
    kfree(buffer);
    buffer = NULL;

    for (index = 0; index < page_count; index++)
    {
        address_space_map_user_page(
            address_space,
            USER_LOAD_ADDR + (index * PAGE_SIZE),
            code_physical + (index * PAGE_SIZE)
        );
    }

    stack_physical = pmm_alloc_contiguous(USER_STACK_PAGES);

    if (stack_physical == 0)
    {
        address_space_destroy(address_space);
        return -1;
    }

    for (index = 0; index < USER_STACK_PAGES; index++)
    {
        address_space_map_user_page(
            address_space,
            USER_STACK_BASE + (index * PAGE_SIZE),
            stack_physical + (index * PAGE_SIZE)
        );
    }

    user_stack_top = setup_user_stack(stack_physical, argc, argv);
    copy_process_name(fs_base_name(resolved), process_name);

    process = process_create_user(
        USER_LOAD_ADDR,
        user_stack_top,
        process_name,
        parent_pid,
        address_space
    );

    if (process == NULL)
    {
        address_space_destroy(address_space);
        return -1;
    }

    result = (int)process->pid;
    return result;
}

int process_kill(uint32_t pid)
{
    return process_terminate(pid);
}
