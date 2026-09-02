#ifndef UACCESS_H
#define UACCESS_H

#include "types.h"

int copy_from_user(void *kernel_destination, uint32_t user_source, size_t length);
int copy_to_user(uint32_t user_destination, const void *kernel_source, size_t length);
int str_from_user(char *kernel_destination, uint32_t user_source, size_t max_length);

#endif
