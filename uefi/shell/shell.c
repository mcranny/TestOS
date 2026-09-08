#include "shell/shell.h"
#include "drivers/console.h"
#include "drivers/fb.h"
#include "drivers/device.h"
#include "arch/io.h"
#include "fs/tfs.h"
#include "fs/fs.h"
#include "fs/path.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "task/process.h"
#include "user/exec.h"
#include "platform.h"
#include "version.h"

#define LINE_MAX 256

static char cwd[FS_MAX_PATH] = "/";

static char fold_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int command_is(const char *line, const char *command)
{
    while (*command) {
        if (fold_lower(*line) != fold_lower(*command)) return 0;
        line++;
        command++;
    }
    return *line == '\0' || *line == ' ';
}

static const char *skip_spaces(const char *s)
{
    while (*s == ' ') s++;
    return s;
}

static void u32_to_dec(uint32_t value, char *out)
{
    char tmp[11];
    int n = 0;
    int i;
    if (value == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (value > 0) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
}

static void print_u32(uint32_t value)
{
    char buf[12];
    u32_to_dec(value, buf);
    console_puts(buf);
}

static void next_arg(const char **cursor, char *out, size_t out_size)
{
    const char *s = skip_spaces(*cursor);
    size_t i = 0;
    while (*s && *s != ' ' && i + 1 < out_size) {
        out[i++] = *s++;
    }
    out[i] = 0;
    *cursor = skip_spaces(s);
}

static int resolve(const char *path, char *out)
{
    return path_resolve(cwd, path, out, FS_MAX_PATH);
}

static void system_reboot(void)
{
    uint8_t status;
    do {
        status = inb(0x64);
    } while (status & 0x02);
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("hlt");
}

static void shell_help(void)
{
    console_puts("Available commands:\n");
    console_puts("  help     - show this message\n");
    console_puts("  version  - show TestOS version\n");
    console_puts("  uptime   - show system uptime\n");
    console_puts("  clear    - clear the screen\n");
    console_puts("  echo     - print text\n");
    console_puts("  mem      - show memory information\n");
    console_puts("  heap     - show heap free bytes\n");
    console_puts("  pci      - list PCI devices\n");
    console_puts("  ls       - list directory\n");
    console_puts("  cd       - change directory\n");
    console_puts("  pwd      - print working directory\n");
    console_puts("  cat      - print a file\n");
    console_puts("  write    - write text to a file\n");
    console_puts("  mkdir    - create a directory\n");
    console_puts("  rm       - remove a file or empty directory\n");
    console_puts("  touch    - create an empty file\n");
    console_puts("  cp       - copy a file\n");
    console_puts("  mv       - move/rename a file\n");
    console_puts("  ./path   - execute a program\n");
    console_puts("  exec     - execute a program\n");
    console_puts("  calc     - run the calculator\n");
    console_puts("  sleep    - block for N timer ticks\n");
    console_puts("  ps       - list processes\n");
    console_puts("  kill     - terminate a process\n");
    console_puts("  fsck     - check filesystem consistency\n");
    console_puts("  reboot   - reboot the system\n");
    console_puts("  halt     - halt the CPU\n");
    console_puts("  panic    - halt the kernel\n");
}

static void shell_version(void)
{
    console_puts(TESTOS_NAME " " TESTOS_VERSION " (x86-64)\n");
}

static void shell_uptime(void)
{
    uint64_t ticks = timer_ticks();
    uint64_t hz = timer_hz();
    uint32_t seconds = (hz != 0) ? (uint32_t)(ticks / hz) : 0;
    console_puts("Uptime: ");
    print_u32(seconds);
    console_puts(" seconds (");
    print_u32((uint32_t)ticks);
    console_puts(" ticks)\n");
}

static void shell_mem(void)
{
    console_puts("Memory:\n  free frames: ");
    print_u32((uint32_t)pmm_free_frames());
    console_puts("\n  total frames: ");
    print_u32((uint32_t)pmm_total_frames());
    console_puts("\n");
}

static void shell_heap(void)
{
    console_puts("Heap free=");
    console_write_hex64(heap_get_free_bytes());
    console_puts(" used=");
    console_write_hex64(heap_get_used_bytes());
    console_puts("\n");
}

static void shell_pci(void)
{
    device_t *dev = device_get_list();
    if (!dev) {
        console_puts("No PCI devices\n");
        return;
    }
    while (dev) {
        console_puts(dev->name ? dev->name : "pci");
        console_puts("\n");
        dev = dev->next;
    }
}

static void shell_echo(const char *args)
{
    args = skip_spaces(args);
    if (*args) console_puts(args);
    console_puts("\n");
}

static void shell_ls(const char *args)
{
    char path[FS_MAX_PATH];
    args = skip_spaces(args);
    if (!tfs_is_mounted()) {
        console_puts("ls: no filesystem\n");
        return;
    }
    if (resolve(*args ? args : ".", path) != 0) {
        console_puts("ls: bad path\n");
        return;
    }
    tfs_list_directory(path);
}

static void shell_cd(const char *args)
{
    char path[FS_MAX_PATH];
    args = skip_spaces(args);
    if (!tfs_is_mounted()) {
        console_puts("cd: no filesystem\n");
        return;
    }
    if (resolve(*args ? args : "/", path) != 0 || !tfs_is_directory(path)) {
        console_puts("cd: failed\n");
        return;
    }
    {
        size_t i = 0;
        while (path[i] && i + 1 < sizeof(cwd)) {
            cwd[i] = path[i];
            i++;
        }
        cwd[i] = 0;
    }
}

static void shell_pwd(void)
{
    console_puts(cwd);
    console_puts("\n");
}

static void shell_mkdir(const char *args)
{
    char path[FS_MAX_PATH];
    args = skip_spaces(args);
    if (!*args || resolve(args, path) != 0 || !tfs_mkdir(path)) {
        console_puts("mkdir: failed\n");
    }
}

static void shell_rm(const char *args)
{
    char path[FS_MAX_PATH];
    args = skip_spaces(args);
    if (!*args || resolve(args, path) != 0 || !tfs_remove(path)) {
        console_puts("rm: failed\n");
    }
}

static void shell_touch(const char *args)
{
    char path[FS_MAX_PATH];
    args = skip_spaces(args);
    if (!*args || resolve(args, path) != 0 || !tfs_touch(path)) {
        console_puts("touch: failed\n");
    }
}

static void shell_cat(const char *args)
{
    char path[FS_MAX_PATH];
    char *buf;
    uint32_t n = 0;
    args = skip_spaces(args);
    if (!*args || resolve(args, path) != 0) {
        console_puts("cat: failed\n");
        return;
    }
    buf = (char *)kmalloc(FS_MAX_FILE_SIZE + 1);
    if (!buf || !tfs_read(path, buf, FS_MAX_FILE_SIZE, &n)) {
        console_puts("cat: failed\n");
        if (buf) kfree(buf);
        return;
    }
    buf[n] = 0;
    console_puts(buf);
    if (n == 0 || buf[n - 1] != '\n') console_puts("\n");
    kfree(buf);
}

static void shell_write(const char *args)
{
    char path[FS_MAX_PATH];
    char name[FS_MAX_PATH];
    const char *rest = skip_spaces(args);
    uint32_t len = 0;
    next_arg(&rest, name, sizeof(name));
    while (rest[len]) len++;
    if (!name[0] || resolve(name, path) != 0 || !tfs_write(path, rest, len, 0)) {
        console_puts("write: failed\n");
    }
}

static void shell_cp(const char *args)
{
    char src[FS_MAX_PATH], dst[FS_MAX_PATH], sres[FS_MAX_PATH], dres[FS_MAX_PATH];
    char *buf;
    uint32_t n = 0;
    const char *rest = skip_spaces(args);
    next_arg(&rest, src, sizeof(src));
    next_arg(&rest, dst, sizeof(dst));
    if (!src[0] || !dst[0] || resolve(src, sres) || resolve(dst, dres)) {
        console_puts("cp: failed\n");
        return;
    }
    buf = (char *)kmalloc(FS_MAX_FILE_SIZE);
    if (!buf || !tfs_read(sres, buf, FS_MAX_FILE_SIZE, &n) || !tfs_write(dres, buf, n, 0)) {
        console_puts("cp: failed\n");
    }
    if (buf) kfree(buf);
}

static void shell_mv(const char *args)
{
    char src[FS_MAX_PATH], dst[FS_MAX_PATH], sres[FS_MAX_PATH], dres[FS_MAX_PATH];
    const char *rest = skip_spaces(args);
    next_arg(&rest, src, sizeof(src));
    next_arg(&rest, dst, sizeof(dst));
    if (!src[0] || !dst[0] || resolve(src, sres) || resolve(dst, dres) || !tfs_rename(sres, dres)) {
        console_puts("mv: failed\n");
    }
}

static void shell_sleep(const char *args)
{
    uint32_t n = 0;
    args = skip_spaces(args);
    while (*args >= '0' && *args <= '9') {
        n = n * 10 + (uint32_t)(*args - '0');
        args++;
    }
    process_sleep_ticks(n ? n : 1);
}

static void shell_ps(void)
{
    uint32_t i;
    console_puts("PID STATE NAME\n");
    for (i = 0; i < process_table_count(); i++) {
        process_t *p = process_at(i);
        const char *st;
        if (!p || p->state == PROC_UNUSED) continue;
        switch (p->state) {
        case PROC_READY: st = "ready"; break;
        case PROC_RUNNING: st = "run"; break;
        case PROC_SLEEPING: st = "sleep"; break;
        case PROC_ZOMBIE: st = "zombie"; break;
        default: st = "?"; break;
        }
        print_u32(p->pid);
        console_puts(" ");
        console_puts(st);
        console_puts(" ");
        console_puts(p->name);
        console_puts("\n");
    }
}

static void shell_kill(const char *args)
{
    uint32_t pid = 0;
    process_t *p;
    args = skip_spaces(args);
    while (*args >= '0' && *args <= '9') {
        pid = pid * 10 + (uint32_t)(*args - '0');
        args++;
    }
    if (!pid) {
        console_puts("kill: usage: kill <pid>\n");
        return;
    }
    p = process_find_by_pid(pid);
    if (p && p->protected) {
        console_puts("kill: cannot kill system process\n");
        return;
    }
    if (process_terminate(pid) != 0) {
        console_puts("kill: failed\n");
    }
}

static void shell_fsck(void)
{
    if (!tfs_is_mounted()) {
        console_puts("fsck: not mounted\n");
        return;
    }
    if (tfs_check() == 0) {
        console_puts("fsck: ok\n");
    } else {
        console_puts("fsck: errors\n");
    }
}

static void shell_exec(const char *line)
{
    char path[FS_MAX_PATH];
    char resolved[FS_MAX_PATH];
    const char *args[8];
    char argbuf[8][64];
    int argc = 0;
    const char *rest = skip_spaces(line);

    if (rest[0] == '.' && rest[1] == '/') {
        rest += 2;
    } else if (command_is(rest, "exec")) {
        rest = skip_spaces(rest + 4);
    }

    next_arg(&rest, path, sizeof(path));
    if (!path[0]) {
        console_puts("exec: usage: ./prog [args]\n");
        return;
    }
    if (path[0] != '/') {
        char tmp[FS_MAX_PATH];
        tmp[0] = '/';
        {
            size_t i = 0;
            while (path[i] && i + 1 < sizeof(tmp) - 1) {
                tmp[i + 1] = path[i];
                i++;
            }
            tmp[i + 1] = 0;
        }
        {
            size_t i = 0;
            while (tmp[i] && i + 1 < sizeof(path)) {
                path[i] = tmp[i];
                i++;
            }
            path[i] = 0;
        }
    }
    if (resolve(path, resolved) != 0) {
        console_puts("exec: bad path\n");
        return;
    }
    args[argc++] = resolved;
    while (argc < 8 && *rest) {
        next_arg(&rest, argbuf[argc], sizeof(argbuf[argc]));
        if (!argbuf[argc][0]) break;
        args[argc] = argbuf[argc];
        argc++;
    }
    if (process_exec(resolved, argc, args) != 0) {
        console_puts("exec: failed\n");
    }
}

static void shell_calc(const char *args)
{
    const char *argv[8];
    char path[] = "/calc";
    char argbuf[6][64];
    int argc = 1;
    const char *rest = skip_spaces(args);
    argv[0] = path;
    while (argc < 7 && *rest) {
        next_arg(&rest, argbuf[argc], sizeof(argbuf[argc]));
        if (!argbuf[argc][0]) break;
        argv[argc] = argbuf[argc];
        argc++;
    }
    if (argc < 2) {
        console_puts("Usage: calc <expression>\n");
        return;
    }
    if (process_exec(path, argc, argv) != 0) {
        console_puts("calc: failed (is /calc seeded?)\n");
    }
}

static int shell_is_exec_path(const char *line)
{
    return line[0] == '.' && line[1] == '/';
}

static void run_line(char *line)
{
    while (*line == ' ') line++;
    if (*line == 0) return;

    if (command_is(line, "help")) { shell_help(); return; }
    if (command_is(line, "version")) { shell_version(); return; }
    if (command_is(line, "uptime")) { shell_uptime(); return; }
    if (command_is(line, "clear")) { fb_console_clear(); return; }
    if (command_is(line, "echo")) { shell_echo(line + 4); return; }
    if (command_is(line, "mem")) { shell_mem(); return; }
    if (command_is(line, "heap")) { shell_heap(); return; }
    if (command_is(line, "pci")) { shell_pci(); return; }
    if (command_is(line, "ls")) { shell_ls(line + 2); return; }
    if (command_is(line, "cd")) { shell_cd(line + 2); return; }
    if (command_is(line, "pwd")) { shell_pwd(); return; }
    if (command_is(line, "mkdir")) { shell_mkdir(line + 5); return; }
    if (command_is(line, "rm")) { shell_rm(line + 2); return; }
    if (command_is(line, "touch")) { shell_touch(line + 5); return; }
    if (command_is(line, "cp")) { shell_cp(line + 2); return; }
    if (command_is(line, "mv")) { shell_mv(line + 2); return; }
    if (command_is(line, "write")) { shell_write(line + 5); return; }
    if (command_is(line, "cat")) { shell_cat(line + 3); return; }
    if (command_is(line, "ps")) { shell_ps(); return; }
    if (command_is(line, "kill")) { shell_kill(line + 4); return; }
    if (command_is(line, "fsck")) { shell_fsck(); return; }
    if (command_is(line, "sleep")) { shell_sleep(line + 5); return; }
    if (command_is(line, "calc")) { shell_calc(line + 4); return; }
    if (command_is(line, "exec")) { shell_exec(line); return; }
    if (command_is(line, "panic")) { panic("initiated by shell"); }
    if (command_is(line, "reboot")) { system_reboot(); }
    if (command_is(line, "halt")) {
        console_puts("halting\n");
        irq_disable();
        for (;;) __asm__ volatile("hlt");
    }
    if (shell_is_exec_path(line)) { shell_exec(line); return; }

    console_puts("Unknown command. Type 'help' for a list of commands.\n");
}

static void print_prompt(void)
{
    console_puts(cwd);
    console_puts("# ");
}

void shell_run(void)
{
    char line[LINE_MAX];
    uint32_t len = 0;

    print_prompt();
    for (;;) {
        char c = console_getchar();
        if (c == '\n') {
            console_putc('\n');
            line[len] = 0;
            run_line(line);
            len = 0;
            print_prompt();
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                console_puts("\b \b");
            }
        } else if (len + 1 < LINE_MAX) {
            line[len++] = c;
            console_putc(c);
        }
    }
}
