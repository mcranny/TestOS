#include "fs/path.h"
#include "fs/fs.h"

static size_t path_len(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static void path_copy(char *dst, const char *src, size_t max)
{
    size_t i = 0;
    if (max == 0) return;
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

int path_resolve(const char *cwd, const char *path, char *out, size_t out_size)
{
    char temp[FS_MAX_PATH];
    size_t ti = 0;
    size_t i = 0;

    if (!cwd || !path || !out || out_size == 0) {
        return -1;
    }

    if (path[0] == '/') {
        path_copy(temp, path, sizeof(temp));
    } else {
        path_copy(temp, cwd, sizeof(temp));
        ti = path_len(temp);
        if (ti == 0 || temp[ti - 1] != '/') {
            if (ti + 1 >= sizeof(temp)) return -1;
            temp[ti++] = '/';
            temp[ti] = 0;
        }
        while (*path && ti + 1 < sizeof(temp)) {
            temp[ti++] = *path++;
        }
        temp[ti] = 0;
    }

    /* Normalize: collapse // and handle . and .. simply by rewriting. */
    out[0] = '/';
    i = 1;
    ti = 0;
    while (temp[ti]) {
        while (temp[ti] == '/') ti++;
        if (!temp[ti]) break;
        if (temp[ti] == '.' && (temp[ti + 1] == '/' || temp[ti + 1] == 0)) {
            ti += 1;
            continue;
        }
        if (temp[ti] == '.' && temp[ti + 1] == '.' &&
            (temp[ti + 2] == '/' || temp[ti + 2] == 0)) {
            ti += 2;
            if (i > 1) {
                i--;
                while (i > 1 && out[i - 1] != '/') i--;
                if (i > 1) i--; /* drop slash before component */
                else i = 1;
            }
            continue;
        }
        if (i > 1) {
            if (i + 1 >= out_size) return -1;
            out[i++] = '/';
        }
        while (temp[ti] && temp[ti] != '/') {
            if (i + 1 >= out_size) return -1;
            out[i++] = temp[ti++];
        }
    }
    if (i == 1) {
        out[0] = '/';
        out[1] = 0;
    } else {
        out[i] = 0;
    }
    return 0;
}
