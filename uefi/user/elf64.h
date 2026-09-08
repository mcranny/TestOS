#ifndef TESTOS_UEFI_USER_ELF64_H
#define TESTOS_UEFI_USER_ELF64_H

#include "types.h"
#include "mm/paging.h"

int elf64_load(address_space_t *as, const void *image, uint64_t size, uint64_t *entry_out);

#endif
