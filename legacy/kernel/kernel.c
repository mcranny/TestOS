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
#include "e1000.h"
#include "ethernet.h"
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
    terminal_initialize_multiboot(magic, (const multiboot_info_t *)mb_info);
    serial_initialize();

    klog(KLOG_INFO, "BOOT", "Kernel loaded; Multiboot handoff accepted");
    if (terminal_framebuffer_active())
    {
        klog(KLOG_INFO, "BOOT", "Framebuffer display initialized");
    }
    else
    {
        klog(KLOG_WARN, "BOOT", "Framebuffer unavailable; using legacy text display");
    }
    terminal_set_boot_stage("KERNEL");

    klog(KLOG_INFO, "BOOT", "Initializing GDT");
    terminal_set_boot_stage("GDT");
    gdt_initialize();

    klog(KLOG_INFO, "BOOT", "Initializing memory map");
    memory_map_initialize(magic, (const multiboot_info_t *)mb_info);
    klog(KLOG_INFO, "BOOT", "Boot information validated; memory map accepted");
    terminal_set_boot_stage("MEMORY");

    klog(KLOG_INFO, "BOOT", "Initializing PMM");
    pmm_initialize();

    klog(KLOG_INFO, "BOOT", "Initializing paging");
    paging_initialize();
    if (terminal_framebuffer_size() != 0)
    {
        (void)paging_map_mmio(terminal_framebuffer_address(), terminal_framebuffer_size());
    }
    klog(KLOG_INFO, "BOOT", "Paging structures and descriptor tables ready");
    terminal_set_boot_stage("PAGING");

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

    klog(KLOG_INFO, "BOOT", "Initializing E1000");
    e1000_initialize();

    interrupts_enable();
    klog(KLOG_INFO, "BOOT", "Interrupt and timer initialization complete");
    terminal_set_boot_stage("INTERRUPTS");

    klog(KLOG_INFO, "BOOT", "Probing network RX");
    net_bootstrap();

    /* This marker is intentionally independent of optional QEMU devices. */
    klog(KLOG_INFO, "BOOT", "TestOS ready");
    terminal_set_boot_stage("TESTOS READY");

    klog(KLOG_INFO, "BOOT", "Starting scheduler");
    process_create_kernel(shell_process_entry, "shell", 0);
    scheduler_start();
}
