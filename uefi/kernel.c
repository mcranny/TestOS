#include "boot/limine_boot.h"
#include "platform.h"
#include "drivers/serial.h"
#include "drivers/fb.h"
#include "drivers/pic.h"
#include "drivers/pit.h"
#include "drivers/kbd.h"
#include "drivers/console.h"
#include "drivers/device.h"
#include "drivers/pci.h"
#include "drivers/ata.h"
#include "drivers/ahci.h"
#include "drivers/nvme.h"
#include "usb/xhci.h"
#include "input/input.h"
#include "e1000.h"
#include "ethernet.h"
#include "netif.h"
#include "arch/tss.h"
#include "arch/gdt.h"
#include "arch/idt.h"
#include "arch/interrupts.h"
#include "arch/apic.h"
#include "arch/acpi.h"
#include "arch/ioapic.h"
#include "mm/pmm.h"
#include "mm/paging.h"
#include "mm/heap.h"
#include "block/block.h"
#include "fs/tfs.h"
#include "cpu/cpu_local.h"
#include "cpu/smp.h"
#include "task/process.h"
#include "user/syscall.h"
#include "user/exec.h"
#include "shell/shell.h"
#include "version.h"

void uefi_main(void)
{
    struct boot_info boot;
    uint64_t wait_ticks;
    block_device_t *disk;

    serial_init();

    if (boot_init(&boot) != 0) {
        panic("boot_init failed");
    }

    fb_init(boot.framebuffer);
    console_init();

    console_puts(TESTOS_NAME " " TESTOS_VERSION " x86-64 boot\n");
    console_puts("kernel entry\n");

    console_puts("BOOT: Boot information validated; framebuffer physical=");
    console_write_hex64((uint64_t)(uintptr_t)boot.framebuffer->address - boot.hhdm_offset);
    console_puts("\n");

    console_puts("loading gdt/idt/tss\n");
    cpu_local_init();
    tss_init_all();
    gdt_init_bsp();
    idt_init();
    interrupts_init();
    console_puts("IDT loaded\n");

    console_puts("initializing physical memory\n");
    pmm_init(&boot);
    console_puts("free frames=");
    console_write_hex64(pmm_free_frames());
    console_puts("\n");

    console_puts("taking over paging\n");
    paging_init(&boot);

    console_puts("initializing heap\n");
    heap_initialize();
    console_puts("heap free=");
    console_write_hex64(heap_get_free_bytes());
    console_puts("\n");

    console_puts("initializing lapic\n");
    lapic_init();
    if (acpi_init(boot.rsdp) == 0) {
        console_puts("acpi madt OK\n");
    } else {
        console_puts("acpi madt missing\n");
    }
    ioapic_init();
    smp_init(boot.smp);
    console_puts("cpu_count=");
    console_write_hex64(cpu_count());
    console_puts("\n");

    paging_user_probe();

    console_puts("scanning pci\n");
    device_initialize();
    pci_initialize();

    console_puts("initializing storage\n");
    block_initialize();
    ata_initialize();
    ahci_initialize();
    nvme_initialize();
    disk = block_pick_boot();
    if (disk && tfs_mount(disk)) {
        console_puts("tfs mount OK");
        if (tfs_was_formatted()) {
            console_puts(" (formatted)");
        }
        console_puts("\n");
    } else {
        console_puts("tfs mount failed\n");
    }

    syscall_init();
    scheduler_init();

    console_puts("enabling timer and keyboard\n");
    pic_remap(0x20, 0x28);
    pic_mask_all();
    kbd_init();
    irq_register(1, kbd_irq_handler);

    /*
     * Park APs before LAPIC calibration. Limine APs busy-spin until
     * goto_address is set; under TCG that steals host time from the BSP
     * and inflates the busy-wait calibration into a near-stuck tick.
     */
    console_puts("starting APs\n");
    smp_start_aps();

    /* BSP LAPIC timer drives the scheduler (APs keep theirs masked). */
    lapic_timer_init(100);

    if (ioapic_present()) {
        interrupts_set_apic_mode(1);
        ioapic_route_isa_irq(1, 33, cpu_local_of(0)->lapic_id);
    } else {
        interrupts_set_apic_mode(0);
        pic_unmask(1);
    }

    input_initialize();
    console_puts("initializing xhci\n");
    xhci_initialize();

    console_puts("initializing e1000\n");
    netif_init();
    e1000_initialize();

    console_puts("BOOT: TestOS ready\n");
    irq_enable();
    smp_enable_scheduling();

    console_puts("probing network\n");
    net_bootstrap();
    scheduler_enable_preempt();

    wait_ticks = timer_ticks();
    while (timer_ticks() - wait_ticks < 5) {
        __asm__ volatile("hlt");
        scheduler_yield();
        irq_enable();
    }
    console_puts("timer ticks=");
    console_write_hex64(timer_ticks());
    console_puts("\n");

    if (scheduler_preempt_probe() != 0) {
        panic("preempt probe failed");
    }

    seed_calc_from_blob();
    seed_net_utils_from_blobs();
    spawn_hello_user();

    console_puts("starting shell\n");
    shell_run();
}
