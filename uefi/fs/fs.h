#ifndef TESTOS_UEFI_FS_FS_H
#define TESTOS_UEFI_FS_FS_H

#include "types.h"

#define FS_MAX_FILES      48
#define FS_MAX_FILE_SIZE  32768
#define FS_MAX_PATH       128
#define FS_MAX_NAME       FS_MAX_PATH

typedef enum
{
    FS_ENTRY_FILE,
    FS_ENTRY_DIRECTORY
} fs_entry_type_t;

#endif
