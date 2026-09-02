#include "fs_selftest.h"
#include "fs.h"
#include "heap.h"
#include "log.h"
#include "memory.h"

static int selftest_fail(const char *step)
{
    klog(KLOG_ERROR, "FS", step);
    return -1;
}

static int selftest_expect_ok(int result, const char *step)
{
    if (!result)
    {
        return selftest_fail(step);
    }

    return 0;
}

static int selftest_write_pattern(const char *path, uint32_t length, uint8_t seed)
{
    char *buffer;
    uint32_t index;
    int ok;

    buffer = (char *)kmalloc(length == 0 ? 1U : length);

    if (buffer == NULL)
    {
        return selftest_fail("selftest: out of memory");
    }

    for (index = 0; index < length; index++)
    {
        buffer[index] = (char)(seed + (uint8_t)(index & 0xFFU));
    }

    ok = fs_write(path, buffer, length);
    kfree(buffer);

    if (!ok)
    {
        return selftest_fail("selftest: write failed");
    }

    return 0;
}

static int selftest_verify_pattern(const char *path, uint32_t length, uint8_t seed)
{
    char *buffer;
    uint32_t bytes_read = 0;
    uint32_t index;

    buffer = (char *)kmalloc(length == 0 ? 1U : length);

    if (buffer == NULL)
    {
        return selftest_fail("selftest: out of memory");
    }

    if (!fs_read(path, buffer, length, &bytes_read) || bytes_read != length)
    {
        kfree(buffer);
        return selftest_fail("selftest: read failed");
    }

    for (index = 0; index < length; index++)
    {
        if ((uint8_t)buffer[index] != (uint8_t)(seed + (uint8_t)(index & 0xFFU)))
        {
            kfree(buffer);
            return selftest_fail("selftest: data mismatch");
        }
    }

    kfree(buffer);
    return 0;
}

int fs_selftest(void)
{
    char name[32];
    uint32_t index;
    uint32_t created = 0;
    char *big;
    uint32_t bytes_read = 0;

    klog(KLOG_INFO, "FS", "selftest: starting");

    if (selftest_expect_ok(fs_mkdir("/test"), "selftest: mkdir /test") != 0)
    {
        /* Directory may already exist from a prior run. */
        if (!fs_is_directory("/test"))
        {
            return -1;
        }
    }

    if (selftest_write_pattern("/test/empty.txt", 0, 0) != 0)
    {
        return -1;
    }

    if (selftest_verify_pattern("/test/empty.txt", 0, 0) != 0)
    {
        return -1;
    }

    if (selftest_write_pattern("/test/small.txt", 64, 0x10) != 0)
    {
        return -1;
    }

    if (selftest_verify_pattern("/test/small.txt", 64, 0x10) != 0)
    {
        return -1;
    }

    if (selftest_write_pattern("/test/big.txt", 1200, 0x20) != 0)
    {
        return -1;
    }

    if (selftest_verify_pattern("/test/big.txt", 1200, 0x20) != 0)
    {
        return -1;
    }

    for (index = 0; index < 5; index++)
    {
        name[0] = '/';
        name[1] = 't';
        name[2] = 'e';
        name[3] = 's';
        name[4] = 't';
        name[5] = '/';
        name[6] = 'm';
        name[7] = (char)('0' + (char)index);
        name[8] = '.';
        name[9] = 't';
        name[10] = 'x';
        name[11] = 't';
        name[12] = '\0';

        if (selftest_write_pattern(name, 32, (uint8_t)(0x30 + index)) != 0)
        {
            return -1;
        }

        created++;
    }

    if (!fs_remove("/test/m0.txt") || fs_exists("/test/m0.txt"))
    {
        return selftest_fail("selftest: delete failed");
    }

    if (selftest_write_pattern("/test/m0.txt", 40, 0xAA) != 0)
    {
        return -1;
    }

    if (selftest_verify_pattern("/test/m0.txt", 40, 0xAA) != 0)
    {
        return -1;
    }

    if (fs_write("/no/such/path.txt", "x", 1))
    {
        return selftest_fail("selftest: invalid path should fail");
    }

    if (fs_read("/test/missing.txt", name, sizeof(name), &bytes_read))
    {
        return selftest_fail("selftest: missing read should fail");
    }

    /* Fill until writes fail, then clean up fill files. */
    for (index = 0; index < FS_MAX_FILES; index++)
    {
        name[0] = '/';
        name[1] = 't';
        name[2] = 'e';
        name[3] = 's';
        name[4] = 't';
        name[5] = '/';
        name[6] = 'f';
        name[7] = (char)('0' + (char)(index / 10U));
        name[8] = (char)('0' + (char)(index % 10U));
        name[9] = '\0';

        if (!fs_write(name, "fill", 4))
        {
            break;
        }

        created++;
    }

    for (index = 0; index < FS_MAX_FILES; index++)
    {
        name[0] = '/';
        name[1] = 't';
        name[2] = 'e';
        name[3] = 's';
        name[4] = 't';
        name[5] = '/';
        name[6] = 'f';
        name[7] = (char)('0' + (char)(index / 10U));
        name[8] = (char)('0' + (char)(index % 10U));
        name[9] = '\0';

        if (fs_exists(name))
        {
            fs_remove(name);
        }
    }

    big = (char *)kmalloc(FS_MAX_FILE_SIZE + 1U);

    if (big != NULL)
    {
        if (fs_write("/test/too_big.txt", big, FS_MAX_FILE_SIZE + 1U))
        {
            kfree(big);
            return selftest_fail("selftest: oversize write should fail");
        }

        kfree(big);
    }

    if (fs_check() != 0)
    {
        return selftest_fail("selftest: tfs_check failed");
    }

    (void)created;
    klog(KLOG_INFO, "FS", "selftest: passed");
    return 0;
}
