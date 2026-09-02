#include "tfs.h"
#include "memory.h"
#include "terminal.h"

#define TFS_MAGIC           0x31534654U /* 'TFS1' LE */
#define TFS_VERSION         1U
#define TFS_INODE_COUNT     FS_MAX_FILES
#define TFS_SECTOR_SIZE     512U
#define TFS_BITMAP_LBA      1U
#define TFS_BITMAP_BLOCKS   8U
#define TFS_INODE_LBA       9U
#define TFS_DATA_LBA        (TFS_INODE_LBA + TFS_INODE_COUNT)

typedef struct tfs_superblock
{
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t inode_count;
    uint32_t bitmap_lba;
    uint32_t bitmap_blocks;
    uint32_t inode_lba;
    uint32_t data_lba;
    uint8_t reserved[512 - 36];
} tfs_superblock_t;

typedef struct tfs_inode
{
    uint32_t used;
    uint32_t type;
    uint32_t size;
    uint32_t executable;
    uint32_t start_block;
    uint32_t block_count;
    char path[FS_MAX_PATH];
    uint8_t reserved[512 - 24 - FS_MAX_PATH];
} tfs_inode_t;

static block_device_t *tfs_device;
static tfs_superblock_t tfs_sb;
static uint8_t tfs_bitmap[TFS_BITMAP_BLOCKS * TFS_SECTOR_SIZE];
static int tfs_mounted;
static int tfs_formatted_on_mount;

static int tfs_string_equals(const char *left, const char *right)
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

static uint32_t tfs_string_length(const char *string)
{
    uint32_t length = 0;

    while (string[length] != '\0')
    {
        length++;
    }

    return length;
}

static void tfs_path_copy(char *destination, const char *source, uint32_t max_length)
{
    uint32_t index = 0;

    while (source[index] != '\0' && index + 1 < max_length)
    {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static const char *tfs_base_name(const char *path)
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

static int tfs_path_is_direct_child(const char *parent, const char *child)
{
    uint32_t parent_length = tfs_string_length(parent);
    uint32_t index;

    if (tfs_string_equals(parent, "/"))
    {
        if (child[0] != '/')
        {
            return 0;
        }

        index = 1;

        while (child[index] != '\0')
        {
            if (child[index] == '/')
            {
                return 0;
            }

            index++;
        }

        return index > 1;
    }

    if (tfs_string_length(child) <= parent_length + 1)
    {
        return 0;
    }

    for (index = 0; index < parent_length; index++)
    {
        if (parent[index] != child[index])
        {
            return 0;
        }
    }

    if (child[parent_length] != '/')
    {
        return 0;
    }

    index = parent_length + 1;

    while (child[index] != '\0')
    {
        if (child[index] == '/')
        {
            return 0;
        }

        index++;
    }

    return 1;
}

static int tfs_flush_superblock(void)
{
    return block_write(tfs_device, 0, 1, &tfs_sb);
}

static int tfs_flush_bitmap(void)
{
    return block_write(
        tfs_device,
        tfs_sb.bitmap_lba,
        tfs_sb.bitmap_blocks,
        tfs_bitmap
    );
}

static int tfs_load_inode(uint32_t index, tfs_inode_t *inode)
{
    if (index >= tfs_sb.inode_count)
    {
        return 0;
    }

    return block_read(tfs_device, tfs_sb.inode_lba + index, 1, inode);
}

static int tfs_store_inode(uint32_t index, const tfs_inode_t *inode)
{
    if (index >= tfs_sb.inode_count)
    {
        return 0;
    }

    return block_write(tfs_device, tfs_sb.inode_lba + index, 1, inode);
}

static uint32_t tfs_data_block_count(void)
{
    if (tfs_sb.block_count <= tfs_sb.data_lba)
    {
        return 0;
    }

    return tfs_sb.block_count - tfs_sb.data_lba;
}

static int tfs_bitmap_get(uint32_t bit)
{
    return (tfs_bitmap[bit / 8U] >> (bit % 8U)) & 1U;
}

static void tfs_bitmap_set(uint32_t bit, int value)
{
    if (value)
    {
        tfs_bitmap[bit / 8U] |= (uint8_t)(1U << (bit % 8U));
    }
    else
    {
        tfs_bitmap[bit / 8U] &= (uint8_t)~(1U << (bit % 8U));
    }
}

static int tfs_alloc_blocks(uint32_t count, uint32_t *start_out)
{
    uint32_t total = tfs_data_block_count();
    uint32_t start;
    uint32_t run;
    uint32_t bit;

    if (count == 0 || count > total)
    {
        return 0;
    }

    for (start = 0; start + count <= total; start++)
    {
        run = 0;

        for (bit = 0; bit < count; bit++)
        {
            if (tfs_bitmap_get(start + bit))
            {
                break;
            }

            run++;
        }

        if (run == count)
        {
            for (bit = 0; bit < count; bit++)
            {
                tfs_bitmap_set(start + bit, 1);
            }

            if (!tfs_flush_bitmap())
            {
                return 0;
            }

            *start_out = tfs_sb.data_lba + start;
            return 1;
        }
    }

    return 0;
}

static void tfs_free_blocks(uint32_t start_lba, uint32_t count)
{
    uint32_t bit;
    uint32_t index;

    if (start_lba < tfs_sb.data_lba || count == 0)
    {
        return;
    }

    index = start_lba - tfs_sb.data_lba;

    for (bit = 0; bit < count; bit++)
    {
        if (index + bit < tfs_data_block_count())
        {
            tfs_bitmap_set(index + bit, 0);
        }
    }

    tfs_flush_bitmap();
}

static int tfs_find_inode(const char *path, uint32_t *index_out, tfs_inode_t *inode_out)
{
    uint32_t index;
    tfs_inode_t inode;

    for (index = 0; index < tfs_sb.inode_count; index++)
    {
        if (!tfs_load_inode(index, &inode))
        {
            return 0;
        }

        if (inode.used && tfs_string_equals(inode.path, path))
        {
            if (index_out != NULL)
            {
                *index_out = index;
            }

            if (inode_out != NULL)
            {
                *inode_out = inode;
            }

            return 1;
        }
    }

    return 0;
}

static int tfs_alloc_inode(uint32_t *index_out)
{
    uint32_t index;
    tfs_inode_t inode;

    for (index = 0; index < tfs_sb.inode_count; index++)
    {
        if (!tfs_load_inode(index, &inode))
        {
            return 0;
        }

        if (!inode.used)
        {
            *index_out = index;
            return 1;
        }
    }

    return 0;
}

static int tfs_clear_inode(uint32_t index)
{
    tfs_inode_t inode;

    memset(&inode, 0, sizeof(inode));
    return tfs_store_inode(index, &inode);
}

int tfs_format(block_device_t *device)
{
    uint32_t index;
    tfs_inode_t inode;

    if (device == NULL || device->block_size != TFS_SECTOR_SIZE)
    {
        return 0;
    }

    tfs_device = device;
    memset(&tfs_sb, 0, sizeof(tfs_sb));
    tfs_sb.magic = TFS_MAGIC;
    tfs_sb.version = TFS_VERSION;
    tfs_sb.block_size = TFS_SECTOR_SIZE;
    tfs_sb.block_count = device->block_count;
    tfs_sb.inode_count = TFS_INODE_COUNT;
    tfs_sb.bitmap_lba = TFS_BITMAP_LBA;
    tfs_sb.bitmap_blocks = TFS_BITMAP_BLOCKS;
    tfs_sb.inode_lba = TFS_INODE_LBA;
    tfs_sb.data_lba = TFS_DATA_LBA;

    if (tfs_sb.block_count <= tfs_sb.data_lba)
    {
        return 0;
    }

    if (!tfs_flush_superblock())
    {
        return 0;
    }

    memset(tfs_bitmap, 0, sizeof(tfs_bitmap));

    if (!tfs_flush_bitmap())
    {
        return 0;
    }

    memset(&inode, 0, sizeof(inode));

    for (index = 0; index < tfs_sb.inode_count; index++)
    {
        if (!tfs_store_inode(index, &inode))
        {
            return 0;
        }
    }

    tfs_mounted = 1;
    return 1;
}

int tfs_mount(block_device_t *device)
{
    tfs_superblock_t loaded;

    tfs_mounted = 0;
    tfs_formatted_on_mount = 0;
    tfs_device = NULL;

    if (device == NULL || device->block_size != TFS_SECTOR_SIZE)
    {
        return 0;
    }

    if (!block_read(device, 0, 1, &loaded))
    {
        return 0;
    }

    if (loaded.magic != TFS_MAGIC ||
        loaded.version != TFS_VERSION ||
        loaded.block_size != TFS_SECTOR_SIZE ||
        loaded.inode_count != TFS_INODE_COUNT ||
        loaded.bitmap_lba != TFS_BITMAP_LBA ||
        loaded.bitmap_blocks != TFS_BITMAP_BLOCKS ||
        loaded.inode_lba != TFS_INODE_LBA ||
        loaded.data_lba != TFS_DATA_LBA)
    {
        if (!tfs_format(device))
        {
            return 0;
        }

        tfs_formatted_on_mount = 1;
        return 1;
    }

    tfs_device = device;
    tfs_sb = loaded;

    if (!block_read(
            tfs_device,
            tfs_sb.bitmap_lba,
            tfs_sb.bitmap_blocks,
            tfs_bitmap))
    {
        tfs_device = NULL;
        return 0;
    }

    tfs_mounted = 1;
    return 1;
}

int tfs_is_mounted(void)
{
    return tfs_mounted;
}

int tfs_was_formatted(void)
{
    return tfs_formatted_on_mount;
}

int tfs_exists(const char *path)
{
    if (!tfs_mounted || path == NULL)
    {
        return 0;
    }

    if (tfs_string_equals(path, "/"))
    {
        return 1;
    }

    return tfs_find_inode(path, NULL, NULL);
}

int tfs_is_directory(const char *path)
{
    tfs_inode_t inode;

    if (!tfs_mounted || path == NULL)
    {
        return 0;
    }

    if (tfs_string_equals(path, "/"))
    {
        return 1;
    }

    if (!tfs_find_inode(path, NULL, &inode))
    {
        return 0;
    }

    return inode.type == (uint32_t)FS_ENTRY_DIRECTORY;
}

int tfs_is_executable(const char *path)
{
    tfs_inode_t inode;

    if (!tfs_mounted || path == NULL)
    {
        return 0;
    }

    if (!tfs_find_inode(path, NULL, &inode))
    {
        return 0;
    }

    return inode.type == (uint32_t)FS_ENTRY_FILE && inode.executable != 0;
}

static int tfs_directory_is_empty(const char *path)
{
    uint32_t index;
    tfs_inode_t inode;

    for (index = 0; index < tfs_sb.inode_count; index++)
    {
        if (!tfs_load_inode(index, &inode))
        {
            return 0;
        }

        if (!inode.used)
        {
            continue;
        }

        if (tfs_string_equals(inode.path, path))
        {
            continue;
        }

        if (tfs_path_is_direct_child(path, inode.path))
        {
            return 0;
        }
    }

    return 1;
}

int tfs_mkdir(const char *path)
{
    uint32_t index;
    tfs_inode_t inode;

    if (!tfs_mounted || path == NULL || tfs_string_equals(path, "/"))
    {
        return 0;
    }

    if (tfs_find_inode(path, NULL, NULL))
    {
        return 1;
    }

    if (!tfs_alloc_inode(&index))
    {
        return 0;
    }

    memset(&inode, 0, sizeof(inode));
    inode.used = 1;
    inode.type = (uint32_t)FS_ENTRY_DIRECTORY;
    tfs_path_copy(inode.path, path, FS_MAX_PATH);
    return tfs_store_inode(index, &inode);
}

int tfs_touch(const char *path)
{
    uint32_t index;
    tfs_inode_t inode;

    if (!tfs_mounted || path == NULL || tfs_string_equals(path, "/"))
    {
        return 0;
    }

    if (tfs_find_inode(path, NULL, &inode))
    {
        return inode.type == (uint32_t)FS_ENTRY_FILE;
    }

    if (!tfs_alloc_inode(&index))
    {
        return 0;
    }

    memset(&inode, 0, sizeof(inode));
    inode.used = 1;
    inode.type = (uint32_t)FS_ENTRY_FILE;
    tfs_path_copy(inode.path, path, FS_MAX_PATH);
    return tfs_store_inode(index, &inode);
}

int tfs_remove(const char *path)
{
    uint32_t index;
    tfs_inode_t inode;

    if (!tfs_mounted || path == NULL || tfs_string_equals(path, "/"))
    {
        return 0;
    }

    if (!tfs_find_inode(path, &index, &inode))
    {
        return 0;
    }

    if (inode.type == (uint32_t)FS_ENTRY_DIRECTORY &&
        !tfs_directory_is_empty(path))
    {
        return 0;
    }

    if (inode.type == (uint32_t)FS_ENTRY_FILE && inode.block_count > 0)
    {
        tfs_free_blocks(inode.start_block, inode.block_count);
    }

    return tfs_clear_inode(index);
}

int tfs_write(
    const char *path,
    const void *data,
    uint32_t length,
    int executable
)
{
    uint32_t index;
    tfs_inode_t inode;
    uint32_t blocks_needed;
    uint32_t start_block = 0;
    uint8_t sector[TFS_SECTOR_SIZE];
    uint32_t offset;
    uint32_t chunk;
    const uint8_t *bytes = (const uint8_t *)data;

    if (!tfs_mounted || path == NULL || length > FS_MAX_FILE_SIZE)
    {
        return 0;
    }

    if (length > 0 && data == NULL)
    {
        return 0;
    }

    if (tfs_find_inode(path, &index, &inode))
    {
        if (inode.type != (uint32_t)FS_ENTRY_FILE)
        {
            return 0;
        }

        if (inode.block_count > 0)
        {
            tfs_free_blocks(inode.start_block, inode.block_count);
            inode.start_block = 0;
            inode.block_count = 0;
            inode.size = 0;
        }
    }
    else
    {
        if (!tfs_alloc_inode(&index))
        {
            return 0;
        }

        memset(&inode, 0, sizeof(inode));
        inode.used = 1;
        inode.type = (uint32_t)FS_ENTRY_FILE;
        tfs_path_copy(inode.path, path, FS_MAX_PATH);
    }

    blocks_needed = (length + TFS_SECTOR_SIZE - 1U) / TFS_SECTOR_SIZE;

    if (blocks_needed > 0)
    {
        if (!tfs_alloc_blocks(blocks_needed, &start_block))
        {
            return 0;
        }

        for (offset = 0; offset < length; offset += TFS_SECTOR_SIZE)
        {
            memset(sector, 0, sizeof(sector));
            chunk = length - offset;

            if (chunk > TFS_SECTOR_SIZE)
            {
                chunk = TFS_SECTOR_SIZE;
            }

            memcpy(sector, bytes + offset, chunk);

            if (!block_write(
                    tfs_device,
                    start_block + (offset / TFS_SECTOR_SIZE),
                    1,
                    sector))
            {
                tfs_free_blocks(start_block, blocks_needed);
                return 0;
            }
        }
    }

    inode.size = length;
    inode.executable = executable ? 1U : 0U;
    inode.start_block = start_block;
    inode.block_count = blocks_needed;
    return tfs_store_inode(index, &inode);
}

int tfs_read(
    const char *path,
    void *buffer,
    uint32_t buffer_size,
    uint32_t *bytes_read
)
{
    tfs_inode_t inode;
    uint8_t sector[TFS_SECTOR_SIZE];
    uint32_t to_copy;
    uint32_t offset;
    uint32_t chunk;
    uint8_t *out = (uint8_t *)buffer;

    if (bytes_read != NULL)
    {
        *bytes_read = 0;
    }

    if (!tfs_mounted || path == NULL || buffer == NULL)
    {
        return 0;
    }

    if (!tfs_find_inode(path, NULL, &inode))
    {
        return 0;
    }

    if (inode.type != (uint32_t)FS_ENTRY_FILE)
    {
        return 0;
    }

    to_copy = inode.size;

    if (to_copy > buffer_size)
    {
        to_copy = buffer_size;
    }

    for (offset = 0; offset < to_copy; offset += TFS_SECTOR_SIZE)
    {
        if (!block_read(
                tfs_device,
                inode.start_block + (offset / TFS_SECTOR_SIZE),
                1,
                sector))
        {
            return 0;
        }

        chunk = to_copy - offset;

        if (chunk > TFS_SECTOR_SIZE)
        {
            chunk = TFS_SECTOR_SIZE;
        }

        memcpy(out + offset, sector, chunk);
    }

    if (bytes_read != NULL)
    {
        *bytes_read = to_copy;
    }

    if (to_copy < buffer_size)
    {
        out[to_copy] = '\0';
    }

    return 1;
}

static void tfs_write_size(uint32_t value)
{
    char buffer[12];
    int index = 0;

    if (value == 0)
    {
        terminal_putchar('0');
        return;
    }

    while (value > 0)
    {
        buffer[index++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (index > 0)
    {
        terminal_putchar(buffer[--index]);
    }
}

void tfs_list_directory(const char *path)
{
    uint32_t index;
    tfs_inode_t inode;
    const char *base_name;

    if (!tfs_mounted || path == NULL)
    {
        terminal_write("(no filesystem)\n");
        return;
    }

    for (index = 0; index < tfs_sb.inode_count; index++)
    {
        if (!tfs_load_inode(index, &inode))
        {
            continue;
        }

        if (!inode.used || !tfs_path_is_direct_child(path, inode.path))
        {
            continue;
        }

        base_name = tfs_base_name(inode.path);
        terminal_write(base_name);

        if (inode.type == (uint32_t)FS_ENTRY_DIRECTORY)
        {
            terminal_write("/");
        }
        else if (inode.executable)
        {
            terminal_write("*");
        }

        if (inode.type == (uint32_t)FS_ENTRY_FILE)
        {
            terminal_write(" (");
            tfs_write_size(inode.size);
            terminal_write(" bytes)");
        }

        terminal_putchar('\n');
    }
}
