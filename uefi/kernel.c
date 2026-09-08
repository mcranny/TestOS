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
#include "arch/tss.h"
#include "arch/gdt.h"
#include "arch/idt.h"
#include "arch/interrupts.h"
#include "mm/pmm.h"
#include "mm/paging.h"
#include "mm/heap.h"
#include "block/block.h"
#include "fs/tfs.h"
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
    tss_init();
    gdt_init();
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

    paging_user_probe();

    console_puts("scanning pci\n");
    device_initialize();
    pci_initialize();

    console_puts("initializing storage\n");
    block_initialize();
    ata_initialize();
    disk = block_get("hd0");
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
    pit_init(100);
    irq_register(0, timer_irq_handler);
    kbd_init();
    irq_register(1, kbd_irq_handler);
    pic_unmask(0);
    pic_unmask(1);

    console_puts("BOOT: TestOS ready\n");
    irq_enable();

    wait_ticks = timer_ticks();
    while (timer_ticks() - wait_ticks < 5) {
        __asm__ volatile("hlt");
        scheduler_yield();
    }
    console_puts("timer ticks=");
    console_write_hex64(timer_ticks());
    console_puts("\n");

    if (scheduler_preempt_probe() != 0) {
        panic("preempt probe failed");
    }

    seed_calc_from_blob();
    spawn_hello_user();

    console_puts("starting shell\n");
    shell_run();
}
