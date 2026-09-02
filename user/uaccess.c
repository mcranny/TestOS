#include "uaccess.h"
#include "process.h"
#include "paging.h"
#include "memory.h"
#include "syscall_abi.h"

static int uaccess_require_user_caller(void)
{
    process_t *process = process_get_current();

    if (process == NULL || !process->is_user)
    {
        return -SYSCALL_ERR_PERM;
    }

    return 0;
}

int copy_from_user(void *kernel_destination, uint32_t user_source, size_t length)
{
    process_t *process = process_get_current();
    int error;

    error = uaccess_require_user_caller();

    if (error != 0)
    {
        return error;
    }

    if (kernel_destination == NULL)
    {
        return -SYSCALL_ERR_INVAL;
    }

    if (length == 0)
    {
        return 0;
    }

    if (user_source == 0)
    {
        return -SYSCALL_ERR_FAULT;
    }

    if (!address_space_is_user_range(process->address_space, user_source, length))
    {
        return -SYSCALL_ERR_FAULT;
    }

    memcpy(kernel_destination, (const void *)user_source, length);
    return 0;
}

int copy_to_user(uint32_t user_destination, const void *kernel_source, size_t length)
{
    process_t *process = process_get_current();
    int error;

    error = uaccess_require_user_caller();

    if (error != 0)
    {
        return error;
    }

    if (kernel_source == NULL)
    {
        return -SYSCALL_ERR_INVAL;
    }

    if (length == 0)
    {
        return 0;
    }

    if (user_destination == 0)
    {
        return -SYSCALL_ERR_FAULT;
    }

    if (!address_space_is_user_range(process->address_space, user_destination, length))
    {
        return -SYSCALL_ERR_FAULT;
    }

    memcpy((void *)user_destination, kernel_source, length);
    return 0;
}

int str_from_user(char *kernel_destination, uint32_t user_source, size_t max_length)
{
    process_t *process = process_get_current();
    size_t index;
    int error;

    error = uaccess_require_user_caller();

    if (error != 0)
    {
        return error;
    }

    if (kernel_destination == NULL || max_length == 0)
    {
        return -SYSCALL_ERR_INVAL;
    }

    if (user_source == 0)
    {
        return -SYSCALL_ERR_FAULT;
    }

    for (index = 0; index + 1 < max_length; index++)
    {
        if ((index & (PAGE_SIZE - 1U)) == 0)
        {
            if (!address_space_is_user_page(
                    process->address_space,
                    user_source + (uint32_t)index))
            {
                return -SYSCALL_ERR_FAULT;
            }
        }

        kernel_destination[index] = ((const char *)user_source)[index];

        if (kernel_destination[index] == '\0')
        {
            return (int)index;
        }
    }

    kernel_destination[max_length - 1] = '\0';
    return -SYSCALL_ERR_INVAL;
}
