#include "fs/ramfs.h"
#include "platform.h"

#define RAMFS_MAX_NODES 128
#define RAMFS_MAX_FILE 4096

/* RAMFS_NAME comes from ramfs.h */
struct ramfs_node {
    char name[RAMFS_NAME];
    int used;
    int is_dir;
    int parent;
    int child;
    int next;
    char data[RAMFS_MAX_FILE];
    size_t size;
};

static struct ramfs_node nodes[RAMFS_MAX_NODES];
static int root_idx;
static int cwd_idx;

static void name_copy(char *dst, const char *src)
{
    size_t i;
    for (i = 0; i + 1 < RAMFS_NAME && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

static int name_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int alloc_node(const char *name, int is_dir, int parent)
{
    int i;
    for (i = 0; i < RAMFS_MAX_NODES; i++) {
        if (!nodes[i].used) {
            nodes[i].used = 1;
            nodes[i].is_dir = is_dir;
            nodes[i].parent = parent;
            nodes[i].child = -1;
            nodes[i].next = -1;
            nodes[i].size = 0;
            nodes[i].data[0] = 0;
            name_copy(nodes[i].name, name);
            return i;
        }
    }
    return -1;
}

static void link_child(int parent, int child)
{
    nodes[child].next = nodes[parent].child;
    nodes[parent].child = child;
}

static int find_child(int parent, const char *name)
{
    int i = nodes[parent].child;
    while (i >= 0) {
        if (name_eq(nodes[i].name, name)) return i;
        i = nodes[i].next;
    }
    return -1;
}

static int split_path(const char *path, int *dir_out, char *leaf_out)
{
    char part[RAMFS_NAME];
    size_t pi = 0;
    int dir = (*path == '/') ? root_idx : cwd_idx;
    const char *p = path;

    if (*p == '/') p++;
    leaf_out[0] = 0;

    while (*p) {
        if (*p == '/') {
            part[pi] = 0;
            if (pi > 0) {
                int next = find_child(dir, part);
                if (next < 0 || !nodes[next].is_dir) return -1;
                dir = next;
            }
            pi = 0;
            p++;
            continue;
        }
        if (pi + 1 >= RAMFS_NAME) return -1;
        part[pi++] = *p++;
    }
    part[pi] = 0;
    if (pi == 0) {
        *dir_out = dir;
        leaf_out[0] = 0;
        return 0;
    }
    if (name_eq(part, ".")) {
        *dir_out = dir;
        leaf_out[0] = 0;
        return 0;
    }
    if (name_eq(part, "..")) {
        if (nodes[dir].parent >= 0) dir = nodes[dir].parent;
        *dir_out = dir;
        leaf_out[0] = 0;
        return 0;
    }
    *dir_out = dir;
    name_copy(leaf_out, part);
    return 0;
}

static int resolve(const char *path, int must_exist, int *is_dir_out)
{
    int dir;
    char leaf[RAMFS_NAME];
    int node;

    if (!path || path[0] == 0) {
        if (is_dir_out) *is_dir_out = 1;
        return cwd_idx;
    }
    if (name_eq(path, "/")) {
        if (is_dir_out) *is_dir_out = 1;
        return root_idx;
    }
    if (split_path(path, &dir, leaf) != 0) return -1;
    if (leaf[0] == 0) {
        if (is_dir_out) *is_dir_out = 1;
        return dir;
    }
    node = find_child(dir, leaf);
    if (node < 0) {
        return must_exist ? -1 : -2; /* -2: parent ok, leaf missing */
    }
    if (is_dir_out) *is_dir_out = nodes[node].is_dir;
    return node;
}

void ramfs_init(void)
{
    int i;
    for (i = 0; i < RAMFS_MAX_NODES; i++) {
        nodes[i].used = 0;
        nodes[i].parent = -1;
        nodes[i].child = -1;
        nodes[i].next = -1;
    }
    root_idx = alloc_node("", 1, -1);
    cwd_idx = root_idx;
}

const char *ramfs_cwd(void)
{
    static char path[256];
    ramfs_pwd(path, sizeof(path));
    return path;
}

int ramfs_pwd(char *out, size_t out_size)
{
    int stack[64];
    int depth = 0;
    int n = cwd_idx;
    size_t o = 0;

    if (out_size == 0) return -1;
    while (n >= 0 && depth < 64) {
        stack[depth++] = n;
        n = nodes[n].parent;
    }
    if (depth == 0 || stack[depth - 1] != root_idx) {
        out[0] = '/';
        out[1] = 0;
        return 0;
    }
    if (depth == 1) {
        out[0] = '/';
        out[1] = 0;
        return 0;
    }
    for (n = depth - 2; n >= 0; n--) {
        const char *name = nodes[stack[n]].name;
        size_t i;
        if (o + 1 >= out_size) return -1;
        out[o++] = '/';
        for (i = 0; name[i]; i++) {
            if (o + 1 >= out_size) return -1;
            out[o++] = name[i];
        }
    }
    out[o] = 0;
    return 0;
}

int ramfs_cd(const char *path)
{
    int is_dir = 0;
    int node = resolve(path ? path : "/", 1, &is_dir);
    if (node < 0 || !is_dir) return -1;
    cwd_idx = node;
    return 0;
}

int ramfs_mkdir(const char *path)
{
    int dir;
    char leaf[RAMFS_NAME];
    int child;
    if (!path || !*path) return -1;
    if (split_path(path, &dir, leaf) != 0 || leaf[0] == 0) return -1;
    if (find_child(dir, leaf) >= 0) return -1;
    child = alloc_node(leaf, 1, dir);
    if (child < 0) return -1;
    link_child(dir, child);
    return 0;
}

int ramfs_touch(const char *path)
{
    int dir;
    char leaf[RAMFS_NAME];
    int child;
    if (!path || !*path) return -1;
    if (split_path(path, &dir, leaf) != 0 || leaf[0] == 0) return -1;
    if (find_child(dir, leaf) >= 0) return 0;
    child = alloc_node(leaf, 0, dir);
    if (child < 0) return -1;
    link_child(dir, child);
    return 0;
}

int ramfs_rm(const char *path)
{
    int dir;
    char leaf[RAMFS_NAME];
    int node;
    int *link;
    if (!path || !*path || name_eq(path, "/")) return -1;
    if (split_path(path, &dir, leaf) != 0 || leaf[0] == 0) return -1;
    node = find_child(dir, leaf);
    if (node < 0) return -1;
    if (nodes[node].is_dir && nodes[node].child >= 0) return -1;
    if (node == cwd_idx) return -1;
    link = &nodes[dir].child;
    while (*link >= 0) {
        if (*link == node) {
            *link = nodes[node].next;
            nodes[node].used = 0;
            return 0;
        }
        link = &nodes[*link].next;
    }
    return -1;
}

int ramfs_ls(const char *path, void (*emit)(const char *name, int is_dir, void *ctx), void *ctx)
{
    int is_dir = 0;
    int node = resolve(path && *path ? path : ".", 1, &is_dir);
    int child;
    if (node < 0 || !is_dir) return -1;
    child = nodes[node].child;
    while (child >= 0) {
        emit(nodes[child].name, nodes[child].is_dir, ctx);
        child = nodes[child].next;
    }
    return 0;
}

int ramfs_cat(const char *path, void (*emit)(const char *chunk, void *ctx), void *ctx)
{
    int is_dir = 0;
    int node = resolve(path, 1, &is_dir);
    if (node < 0 || is_dir) return -1;
    nodes[node].data[nodes[node].size] = 0;
    emit(nodes[node].data, ctx);
    return 0;
}

int ramfs_write(const char *path, const char *text)
{
    int is_dir = 0;
    int node;
    size_t len = 0;
    if (!path || !*path) return -1;
    if (!text) text = "";
    while (text[len]) len++;
    if (len >= RAMFS_MAX_FILE) return -1;
    node = resolve(path, 1, &is_dir);
    if (node == -2 || node < 0) {
        if (ramfs_touch(path) != 0) return -1;
        node = resolve(path, 1, &is_dir);
    }
    if (node < 0 || is_dir) return -1;
    {
        size_t i;
        for (i = 0; i < len; i++) nodes[node].data[i] = text[i];
        nodes[node].data[len] = 0;
        nodes[node].size = len;
    }
    return 0;
}

int ramfs_cp(const char *src, const char *dst)
{
    int is_dir = 0;
    int node = resolve(src, 1, &is_dir);
    if (node < 0 || is_dir) return -1;
    return ramfs_write(dst, nodes[node].data);
}

int ramfs_mv(const char *src, const char *dst)
{
    if (ramfs_cp(src, dst) != 0) return -1;
    return ramfs_rm(src);
}

int ramfs_fsck(void)
{
    int i;
    int used = 0;
    for (i = 0; i < RAMFS_MAX_NODES; i++) {
        if (!nodes[i].used) continue;
        used++;
        if (nodes[i].parent >= RAMFS_MAX_NODES) return -1;
        if (nodes[i].size >= RAMFS_MAX_FILE) return -1;
    }
    return used;
}

int ramfs_fstest(void)
{
    char path[64];
    int i;
    (void)ramfs_mkdir("/fstest");
    for (i = 0; i < 8; i++) {
        path[0] = '/'; path[1] = 'f'; path[2] = 's'; path[3] = 't'; path[4] = 'e';
        path[5] = 's'; path[6] = 't'; path[7] = '/'; path[8] = 'f';
        path[9] = (char)('0' + i); path[10] = 0;
        if (ramfs_write(path, "ok") != 0) return -1;
        if (ramfs_rm(path) != 0) return -1;
    }
    return 0;
}
