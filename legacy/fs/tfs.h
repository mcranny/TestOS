#ifndef TFS_H
#define TFS_H

#include "types.h"
#include "block.h"
#include "fs.h"

int tfs_mount(block_device_t *device);
int tfs_is_mounted(void);
int tfs_format(block_device_t *device);

int tfs_mkdir(const char *path);
int tfs_touch(const char *path);
int tfs_remove(const char *path);
int tfs_exists(const char *path);
int tfs_is_directory(const char *path);
int tfs_is_executable(const char *path);

int tfs_write(
    const char *path,
    const void *data,
    uint32_t length,
    int executable
);
int tfs_read(
    const char *path,
    void *buffer,
    uint32_t buffer_size,
    uint32_t *bytes_read
);
int tfs_rename(const char *old_path, const char *new_path);

void tfs_list_directory(const char *path);
int tfs_was_formatted(void);
int tfs_check(void);

#endif
