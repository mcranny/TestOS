#include "kernel.h"
#include "terminal.h"
#include "gdt.h"
#include "tss.h"
#include "interrupts.h"
#include "memory_map.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "block.h"
#include "ata.h"
#include "fs.h"
#include "process.h"
#include "syscall.h"
#include "exceptions.h"
#include "shell.h"

static void shell_process_entry(void)
{
    shell_run();
}

void kernel_main(uint32_t magic, uint32_t mb_info)
{
    terminal_initialize();

    terminal_write("TestOS booted.\n");

    gdt_initialize();
    memory_map_initialize(magic, (const multiboot_info_t *)mb_info);
    pmm_initialize();
    paging_initialize();
    heap_initialize();
    tss_initialize();
    block_initialize();
    ata_initialize();
    fs_initialize();
    syscall_initialize();
    exceptions_initialize();
    scheduler_initialize();

    interrupts_initialize();
    paging_enable();
    interrupts_enable();

    process_create_kernel(shell_process_entry, "shell", 0);

    scheduler_start();
}
