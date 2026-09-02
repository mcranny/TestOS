#include "kernel.h"
#include "terminal.h"
#include "serial.h"
#include "log.h"
#include "config.h"
#include "gdt.h"
#include "tss.h"
#include "interrupts.h"
#include "memory_map.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "block.h"
#include "ata.h"
#include "device.h"
#include "pci.h"
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
    serial_initialize();

    klog(KLOG_INFO, "BOOT", "Starting kernel");

    klog(KLOG_INFO, "BOOT", "Initializing GDT");
    gdt_initialize();

    klog(KLOG_INFO, "BOOT", "Initializing memory map");
    memory_map_initialize(magic, (const multiboot_info_t *)mb_info);

    klog(KLOG_INFO, "BOOT", "Initializing PMM");
    pmm_initialize();

    klog(KLOG_INFO, "BOOT", "Initializing paging");
    paging_initialize();

    klog(KLOG_INFO, "BOOT", "Initializing heap");
    heap_initialize();

    klog(KLOG_INFO, "BOOT", "Initializing TSS");
    tss_initialize();

    klog(KLOG_INFO, "BOOT", "Initializing block devices");
    block_initialize();

    klog(KLOG_INFO, "BOOT", "Initializing device registry");
    device_initialize();

    klog(KLOG_INFO, "BOOT", "Initializing PCI");
    pci_initialize();

    klog(KLOG_INFO, "BOOT", "Initializing ATA");
    ata_initialize();

    klog(KLOG_INFO, "BOOT", "Mounting TFS");
    fs_initialize();

#ifdef TESTOS_SELFTEST
    klog(KLOG_INFO, "BOOT", "Running filesystem selftest");
    if (fs_selftest() != 0)
    {
        panic("FS", "selftest failed");
    }
    klog(KLOG_INFO, "BOOT", "Filesystem selftest passed");
#endif

    klog(KLOG_INFO, "BOOT", "Initializing syscalls");
    syscall_initialize();

    klog(KLOG_INFO, "BOOT", "Initializing exceptions");
    exceptions_initialize();

    klog(KLOG_INFO, "BOOT", "Initializing scheduler");
    scheduler_initialize();

    klog(KLOG_INFO, "BOOT", "Initializing interrupts");
    interrupts_initialize();

    paging_enable();
    klog(KLOG_INFO, "BOOT", "Paging enabled");

    interrupts_enable();
    klog(KLOG_INFO, "BOOT", "Interrupts enabled");

    klog(KLOG_INFO, "BOOT", "Starting scheduler");
    process_create_kernel(shell_process_entry, "shell", 0);
    scheduler_start();
}
