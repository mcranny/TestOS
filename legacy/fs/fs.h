#ifndef FS_H
#define FS_H

#include "types.h"

#define FS_MAX_FILES      48
#define FS_MAX_FILE_SIZE  8192
#define FS_MAX_PATH       128
#define FS_MAX_NAME       FS_MAX_PATH

typedef enum
{
    FS_ENTRY_FILE,
    FS_ENTRY_DIRECTORY
} fs_entry_type_t;

void fs_initialize(void);
void fs_set_cwd(const char *path);
const char *fs_get_cwd(void);

int fs_resolve_path(const char *path, char *resolved);
int fs_mkdir(const char *path);
int fs_touch(const char *path);
int fs_remove(const char *path);
int fs_is_directory(const char *path);
int fs_is_executable(const char *path);
int fs_exists(const char *path);

int fs_write(const char *path, const char *data, uint32_t length);
int fs_read(const char *path, char *buffer, uint32_t buffer_size, uint32_t *bytes_read);
int fs_install_executable(const char *path, const void *data, uint32_t size);
int fs_copy(const char *src_path, const char *dst_path);
int fs_move(const char *src_path, const char *dst_path);

void fs_list_directory(const char *path);
const char *fs_base_name(const char *path);

int fs_check(void);
int fs_selftest(void);

#endif
