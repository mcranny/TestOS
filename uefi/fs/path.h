#ifndef TESTOS_UEFI_FS_PATH_H
#define TESTOS_UEFI_FS_PATH_H

#include "types.h"

/* Resolve relative path against cwd into out (absolute, FS_MAX_PATH). */
int path_resolve(const char *cwd, const char *path, char *out, size_t out_size);

#endif
