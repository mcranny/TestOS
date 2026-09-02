#include "fs.h"
#include "tfs.h"
#include "block.h"
#include "terminal.h"

static char current_working_directory[FS_MAX_PATH] = "/";

extern uint8_t _binary_build_calc_bin_start[];
extern uint8_t _binary_build_calc_bin_end[];

static int string_equals(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0')
    {
        if (*left != *right)
        {
            return 0;
        }

        left++;
        right++;
    }

    return *left == *right;
}

static uint32_t string_length(const char *string)
{
    uint32_t length = 0;

    while (string[length] != '\0')
    {
        length++;
    }

    return length;
}

static void path_copy(char *destination, const char *source, uint32_t max_length)
{
    uint32_t index = 0;

    while (source[index] != '\0' && index + 1 < max_length)
    {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static int path_is_absolute(const char *path)
{
    return path[0] == '/';
}

static int path_normalize(char *path)
{
    char temp[FS_MAX_PATH];
    char part[FS_MAX_PATH];
    uint32_t temp_length = 0;
    uint32_t index = 0;
    uint32_t part_index = 0;

    if (path[0] != '/')
    {
        return 0;
    }

    temp[0] = '\0';

    while (path[index] != '\0')
    {
        while (path[index] == '/')
        {
            index++;
        }

        if (path[index] == '\0')
        {
            break;
        }

        part_index = 0;

        while (path[index] != '\0' && path[index] != '/')
        {
            if (part_index + 1 >= FS_MAX_PATH)
            {
                return 0;
            }

            part[part_index++] = path[index++];
        }

        part[part_index] = '\0';

        if (string_equals(part, ".") || part[0] == '\0')
        {
            continue;
        }

        if (string_equals(part, ".."))
        {
            if (temp_length == 0)
            {
                return 0;
            }

            while (temp_length > 0 && temp[temp_length - 1] != '/')
            {
                temp_length--;
            }

            if (temp_length > 0)
            {
                temp_length--;
            }

            temp[temp_length] = '\0';
            continue;
        }

        temp[temp_length++] = '/';

        for (part_index = 0; part[part_index] != '\0'; part_index++)
        {
            temp[temp_length++] = part[part_index];
        }

        temp[temp_length] = '\0';
    }

    if (temp_length == 0)
    {
        path_copy(path, "/", FS_MAX_PATH);
        return 1;
    }

    path_copy(path, temp, FS_MAX_PATH);
    return 1;
}

static int path_append(char *base, const char *component, char *output)
{
    char combined[FS_MAX_PATH];
    uint32_t base_length = string_length(base);
    uint32_t index = 0;

    if (path_is_absolute(component))
    {
        path_copy(output, component, FS_MAX_PATH);
        return path_normalize(output);
    }

    if (base_length + 1 + string_length(component) + 1 >= FS_MAX_PATH)
    {
        return 0;
    }

    path_copy(combined, base, FS_MAX_PATH);

    if (base_length > 1 || base[0] != '/')
    {
        combined[base_length] = '/';
        combined[base_length + 1] = '\0';
        index = base_length + 1;
    }
    else
    {
        index = 1;
    }

    while (*component != '\0')
    {
        combined[index++] = *component++;
    }

    combined[index] = '\0';
    path_copy(output, combined, FS_MAX_PATH);
    return path_normalize(output);
}

static int path_parent(const char *path, char *parent)
{
    char temp[FS_MAX_PATH];
    uint32_t length;

    path_copy(temp, path, FS_MAX_PATH);

    if (!path_normalize(temp))
    {
        return 0;
    }

    if (string_equals(temp, "/"))
    {
        path_copy(parent, "/", FS_MAX_PATH);
        return 1;
    }

    length = string_length(temp);

    while (length > 0 && temp[length - 1] != '/')
    {
        length--;
    }

    if (length <= 1)
    {
        path_copy(parent, "/", FS_MAX_PATH);
        return 1;
    }

    temp[length - 1] = '\0';
    path_copy(parent, temp, FS_MAX_PATH);
    return 1;
}

const char *fs_base_name(const char *path)
{
    const char *name = path;
    uint32_t index = 0;

    while (path[index] != '\0')
    {
        if (path[index] == '/' && path[index + 1] != '\0')
        {
            name = &path[index + 1];
        }

        index++;
    }

    return name;
}

void fs_set_cwd(const char *path)
{
    char resolved[FS_MAX_PATH];

    if (path == NULL)
    {
        return;
    }

    path_copy(resolved, path, FS_MAX_PATH);

    if (!path_normalize(resolved))
    {
        return;
    }

    if (!tfs_is_directory(resolved))
    {
        return;
    }

    path_copy(current_working_directory, resolved, FS_MAX_PATH);
}

const char *fs_get_cwd(void)
{
    return current_working_directory;
}

int fs_resolve_path(const char *path, char *resolved)
{
    if (path == NULL || resolved == NULL)
    {
        return 0;
    }

    if (path_is_absolute(path))
    {
        path_copy(resolved, path, FS_MAX_PATH);
        return path_normalize(resolved);
    }

    return path_append(current_working_directory, path, resolved);
}

int fs_mkdir(const char *path)
{
    char resolved[FS_MAX_PATH];
    char parent[FS_MAX_PATH];

    if (!fs_resolve_path(path, resolved))
    {
        return 0;
    }

    if (string_equals(resolved, "/"))
    {
        return 1;
    }

    if (!path_parent(resolved, parent))
    {
        return 0;
    }

    if (!tfs_is_directory(parent))
    {
        return 0;
    }

    return tfs_mkdir(resolved);
}

int fs_touch(const char *path)
{
    char resolved[FS_MAX_PATH];
    char parent[FS_MAX_PATH];

    if (!fs_resolve_path(path, resolved))
    {
        return 0;
    }

    if (!path_parent(resolved, parent))
    {
        return 0;
    }

    if (!tfs_is_directory(parent))
    {
        return 0;
    }

    return tfs_touch(resolved);
}

int fs_remove(const char *path)
{
    char resolved[FS_MAX_PATH];

    if (!fs_resolve_path(path, resolved))
    {
        return 0;
    }

    return tfs_remove(resolved);
}

int fs_is_directory(const char *path)
{
    char resolved[FS_MAX_PATH];

    if (!fs_resolve_path(path, resolved))
    {
        return string_equals(path, "/");
    }

    return tfs_is_directory(resolved);
}

int fs_is_executable(const char *path)
{
    char resolved[FS_MAX_PATH];

    if (!fs_resolve_path(path, resolved))
    {
        return 0;
    }

    return tfs_is_executable(resolved);
}

int fs_exists(const char *path)
{
    char resolved[FS_MAX_PATH];

    if (string_equals(path, "/"))
    {
        return 1;
    }

    if (!fs_resolve_path(path, resolved))
    {
        return 0;
    }

    return tfs_exists(resolved);
}

int fs_write(const char *path, const char *data, uint32_t length)
{
    char resolved[FS_MAX_PATH];
    char parent[FS_MAX_PATH];

    if (!fs_resolve_path(path, resolved))
    {
        return 0;
    }

    if (!path_parent(resolved, parent))
    {
        return 0;
    }

    if (!tfs_is_directory(parent))
    {
        return 0;
    }

    return tfs_write(resolved, data, length, 0);
}

int fs_read(
    const char *path,
    char *buffer,
    uint32_t buffer_size,
    uint32_t *bytes_read
)
{
    char resolved[FS_MAX_PATH];

    if (!fs_resolve_path(path, resolved))
    {
        return 0;
    }

    return tfs_read(resolved, buffer, buffer_size, bytes_read);
}

int fs_install_executable(const char *path, const void *data, uint32_t size)
{
    char resolved[FS_MAX_PATH];
    char parent[FS_MAX_PATH];

    if (path == NULL || path[0] != '/')
    {
        return 0;
    }

    path_copy(resolved, path, FS_MAX_PATH);

    if (!path_normalize(resolved))
    {
        return 0;
    }

    if (!path_parent(resolved, parent))
    {
        return 0;
    }

    if (!tfs_is_directory(parent))
    {
        return 0;
    }

    return tfs_write(resolved, data, size, 1);
}

void fs_list_directory(const char *path)
{
    char resolved[FS_MAX_PATH];

    if (path == NULL || path[0] == '\0')
    {
        path_copy(resolved, current_working_directory, FS_MAX_PATH);
    }
    else if (!fs_resolve_path(path, resolved))
    {
        terminal_write("Directory not found.\n");
        return;
    }

    if (!tfs_is_directory(resolved))
    {
        terminal_write("Not a directory.\n");
        return;
    }

    tfs_list_directory(resolved);
}

void fs_initialize(void)
{
    block_device_t *disk = block_get("hd0");
    uint32_t calc_size =
        (uint32_t)(_binary_build_calc_bin_end - _binary_build_calc_bin_start);

    path_copy(current_working_directory, "/", FS_MAX_PATH);

    if (disk == NULL)
    {
        terminal_write("FS: no block device\n");
        return;
    }

    if (!tfs_mount(disk))
    {
        terminal_write("FS: mount failed\n");
        return;
    }

    if (tfs_was_formatted())
    {
        static const char readme[] =
            "Welcome to TestOS.\nUse ./calc <expr> for the calculator.\n";

        terminal_write("FS: formatted new disk\n");
        fs_mkdir("/bin");
        fs_write("/readme.txt", readme, sizeof(readme) - 1U);
        fs_install_executable(
            "/calc",
            _binary_build_calc_bin_start,
            calc_size
        );
    }
    else
    {
        terminal_write("FS: mounted hd0\n");
    }
}
