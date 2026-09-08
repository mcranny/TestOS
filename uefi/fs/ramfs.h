#ifndef TESTOS_UEFI_FS_RAMFS_H
#define TESTOS_UEFI_FS_RAMFS_H

#include "types.h"

#define RAMFS_NAME 48

void ramfs_init(void);
const char *ramfs_cwd(void);
int ramfs_pwd(char *out, size_t out_size);
int ramfs_cd(const char *path);
int ramfs_mkdir(const char *path);
int ramfs_touch(const char *path);
int ramfs_rm(const char *path);
int ramfs_ls(const char *path, void (*emit)(const char *name, int is_dir, void *ctx), void *ctx);
int ramfs_cat(const char *path, void (*emit)(const char *chunk, void *ctx), void *ctx);
int ramfs_write(const char *path, const char *text);
int ramfs_cp(const char *src, const char *dst);
int ramfs_mv(const char *src, const char *dst);
int ramfs_fsck(void);
int ramfs_fstest(void);

#endif
