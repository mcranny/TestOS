#include "user/exec.h"
#include "user/elf64.h"
#include "task/process.h"
#include "fs/tfs.h"
#include "fs/fs.h"
#include "mm/paging.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "arch/io.h"
#include "lib/string.h"
#include "drivers/console.h"
#include "platform.h"

#define PAGE_SIZE 4096ULL

extern const uint8_t _calc_blob_start[];
extern const uint8_t _calc_blob_end[];

static int map_user_stack(address_space_t *as, uint64_t *stack_top_out)
{
    uint64_t i;
    uint64_t top = USER_STACK_TOP;
    for (i = 0; i < USER_STACK_PAGES; i++) {
        uint64_t virt = top - (i + 1) * PAGE_SIZE;
        uint64_t phys = pmm_alloc_frame();
        if (!phys) return -1;
        memset(phys_to_virt(phys), 0, PAGE_SIZE);
        if (map_user_page(as, virt, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
            return -1;
        }
    }
    *stack_top_out = top;
    return 0;
}

static uint64_t push_string(address_space_t *as, uint64_t sp, const char *s, uint64_t *out_ptr)
{
    size_t len = 0;
    uint8_t *dst;
    uint64_t phys_page;
    (void)as;
    while (s[len]) len++;
    sp -= len + 1;
    sp &= ~0xFULL;
    /* write via walk — for simplicity require stack pages already mapped and use temporary switch */
    {
        uint64_t saved = read_cr3();
        address_space_switch(as);
        dst = (uint8_t *)(uintptr_t)sp;
        memcpy(dst, s, len + 1);
        write_cr3(saved);
        address_space_switch(address_space_kernel());
    }
    (void)phys_page;
    *out_ptr = sp;
    return sp;
}

int process_exec(const char *path, int argc, const char **argv)
{
    char *buffer;
    uint32_t nread = 0;
    address_space_t *as;
    uint64_t entry = 0;
    uint64_t stack_top;
    uint64_t sp;
    uint64_t *argv_ptrs;
    int i;
    process_t *proc;
    uint32_t parent = process_get_current_pid();

    if (!path || !tfs_is_mounted()) return -1;
    if (!tfs_exists(path) || tfs_is_directory(path)) return -1;

    buffer = (char *)kmalloc(FS_MAX_FILE_SIZE);
    if (!buffer) return -1;
    if (!tfs_read(path, buffer, FS_MAX_FILE_SIZE, &nread) || nread == 0) {
        kfree(buffer);
        return -1;
    }

    as = address_space_create();
    if (!as) {
        kfree(buffer);
        return -1;
    }
    if (elf64_load(as, buffer, nread, &entry) != 0) {
        console_puts("exec: elf load failed\n");
        address_space_destroy(as);
        kfree(buffer);
        return -1;
    }
    kfree(buffer);

    if (map_user_stack(as, &stack_top) != 0) {
        console_puts("exec: stack map failed\n");
        address_space_destroy(as);
        return -1;
    }

    sp = stack_top;
    argv_ptrs = (uint64_t *)kmalloc(sizeof(uint64_t) * (uint64_t)(argc + 1));
    if (!argv_ptrs) {
        address_space_destroy(as);
        return -1;
    }
    for (i = 0; i < argc; i++) {
        sp = push_string(as, sp, argv[i] ? argv[i] : "", &argv_ptrs[i]);
    }
    argv_ptrs[argc] = 0;

    /* Build argc/argv on user stack (SysV): [...args][NULL][argv pointers...][argc] */
    sp &= ~0xFULL;
    {
        uint64_t saved = read_cr3();
        uint64_t *usp;
        address_space_switch(as);
        sp -= sizeof(uint64_t) * (uint64_t)(argc + 1);
        usp = (uint64_t *)(uintptr_t)sp;
        for (i = 0; i <= argc; i++) {
            usp[i] = argv_ptrs[i];
        }
        sp -= sizeof(uint64_t);
        *(uint64_t *)(uintptr_t)sp = (uint64_t)argc;
        /* Align for entry: rsp % 16 == 8 before call; with argc at top we use rdi/rsi manually in crt */
        write_cr3(saved);
        address_space_switch(address_space_kernel());
    }
    kfree(argv_ptrs);

    /* Put argc in rdi and argv in rsi via a tiny user stub? Our calc main expects System V.
     * Simpler: entry trampoline in kernel enter_user only sets rip/rsp.
     * Store argc/argv at known stack layout and use a crt0 that reads them.
     * For calc we build with a crt0.S.
     */

    proc = process_create_user(entry, sp, as, path, parent);
    if (!proc) {
        address_space_destroy(as);
        return -1;
    }
    process_wait_pid(proc->pid);
    return 0;
}

/* Tiny ring3 hello: write("hi\n") + exit(0) using syscall ABI. */
int spawn_hello_user(void)
{
    address_space_t *as;
    uint64_t code_phys;
    uint64_t stack_top;
    uint8_t *code;
    process_t *proc;
    /* Machine code at USER_LOAD_ADDR */
    static const uint8_t prog[] = {
        /* lea msg(%rip), %rdi */
        0x48, 0x8d, 0x3d, 0x1c, 0x00, 0x00, 0x00,
        /* mov $3, %rsi */
        0x48, 0xc7, 0xc6, 0x03, 0x00, 0x00, 0x00,
        /* mov $2, %rax  SYS_WRITE */
        0x48, 0xc7, 0xc0, 0x02, 0x00, 0x00, 0x00,
        /* syscall */
        0x0f, 0x05,
        /* xor %rdi,%rdi */
        0x48, 0x31, 0xff,
        /* mov $1, %rax SYS_EXIT */
        0x48, 0xc7, 0xc0, 0x01, 0x00, 0x00, 0x00,
        /* syscall */
        0x0f, 0x05,
        /* msg: "hi\n" */
        'h', 'i', '\n'
    };

    as = address_space_create();
    if (!as) return -1;
    code_phys = pmm_alloc_frame();
    if (!code_phys) {
        address_space_destroy(as);
        return -1;
    }
    code = (uint8_t *)phys_to_virt(code_phys);
    memset(code, 0, PAGE_SIZE);
    memcpy(code, prog, sizeof(prog));
    if (map_user_page(as, USER_LOAD_ADDR, code_phys, PAGE_PRESENT | PAGE_USER) != 0) {
        address_space_destroy(as);
        return -1;
    }
    if (map_user_stack(as, &stack_top) != 0) {
        address_space_destroy(as);
        return -1;
    }
    proc = process_create_user(USER_LOAD_ADDR, stack_top - 16, as, "hello", process_get_current_pid());
    if (!proc) {
        address_space_destroy(as);
        return -1;
    }
    process_wait_pid(proc->pid);
    console_puts("ring3 hello OK\n");
    return 0;
}

int seed_calc_from_blob(void)
{
    uint64_t size = (uint64_t)(_calc_blob_end - _calc_blob_start);
    if (!tfs_is_mounted()) return -1;
    if (size == 0) return -1;
    if (size > FS_MAX_FILE_SIZE) {
        console_puts("seed /calc: blob too large\n");
        return -1;
    }
    if (tfs_exists("/calc")) {
        /* Always refresh from the embedded blob so linker/loader fixes take effect. */
        if (!tfs_remove("/calc")) return -1;
    }
    if (!tfs_write("/calc", _calc_blob_start, (uint32_t)size, 1)) {
        console_puts("seed /calc: write failed\n");
        return -1;
    }
    console_puts("seeded /calc\n");
    return 0;
}
