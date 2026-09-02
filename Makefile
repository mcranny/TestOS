TOOLCHAIN = /c/Users/matthewc/Documents/Project/toolchain/bin
ASM      = nasm
CC       = $(TOOLCHAIN)/i686-elf-gcc
LD       = $(TOOLCHAIN)/i686-elf-ld
GRUB_MKRESCUE = grub-mkrescue
QEMU = "/c/Program Files/qemu/qemu-system-i386.exe"
OBJCOPY  = $(TOOLCHAIN)/i686-elf-objcopy

KERNEL = build/kernel.bin
ISO    = build/testos.iso
DISK   = build/disk.img
DISK_SIZE_MB = 16

CFLAGS = -ffreestanding \
         -fno-pie \
         -fno-stack-protector \
         -nostdlib \
         -nostdinc \
         -Wall \
         -Wextra \
         -Iinclude \
         -Ikernel \
         -Iinterrupts \
         -Ishell \
         -Imemory \
         -Iarch \
         -Icpu \
         -Itask \
         -Ifs \
         -Iuser \
         -Iblock \
         -Idrivers \
         $(CFLAGS_EXTRA)

LDFLAGS = -m elf_i386 -T linker.ld

USER_CFLAGS = -ffreestanding \
              -fno-pie \
              -fno-stack-protector \
              -fno-omit-frame-pointer \
              -fno-asynchronous-unwind-tables \
              -mpreferred-stack-boundary=2 \
              -nostdlib \
              -nostdinc \
              -Wall \
              -Wextra \
              -Iinclude \
              -Iuser

USER_LDFLAGS = -m elf_i386 -T user/user.ld

# Kernel C sources by subsystem (add new drivers under drivers/).
KERNEL_C_SOURCES = \
    kernel/kernel.c \
    kernel/terminal.c \
    kernel/keyboard.c \
    kernel/timer.c \
    kernel/log.c \
    interrupts/interrupts.c \
    interrupts/pic.c \
    shell/shell.c \
    memory/util.c \
    memory/memory_map.c \
    memory/pmm.c \
    memory/paging.c \
    memory/heap.c \
    arch/gdt.c \
    arch/tss.c \
    cpu/exceptions.c \
    task/process.c \
    block/block.c \
    drivers/ata.c \
    drivers/serial.c \
    drivers/device.c \
    drivers/pci.c \
    drivers/mouse.c \
    fs/tfs.c \
    fs/fs.c \
    fs/fs_selftest.c \
    user/exec.c \
    user/syscall.c \
    user/uaccess.c

C_OBJECTS = $(patsubst %.c,build/%.o,$(notdir $(KERNEL_C_SOURCES)))

ASM_OBJECTS = \
    build/boot.o \
    build/interrupts_asm.o \
    build/exceptions_asm.o \
    build/gdt_asm.o \
    build/tss_asm.o \
    build/switch_asm.o \
    build/syscall_asm.o \
    build/calc_data.o

.PHONY: all iso clean run disk-reset selftest-run

all: $(KERNEL)

build:
	mkdir -p build

# --- User calculator program ---

build/crt0.o: user/crt0.asm | build
	$(ASM) -f elf32 $< -o $@

build/calc.o: user/calc.c | build
	$(CC) $(USER_CFLAGS) -c $< -o $@

build/ulib.o: user/ulib.c | build
	$(CC) $(USER_CFLAGS) -c $< -o $@

build/calc.elf: build/crt0.o build/calc.o build/ulib.o user/user.ld | build
	$(LD) $(USER_LDFLAGS) -o $@ build/crt0.o build/calc.o build/ulib.o

build/calc.bin: build/calc.elf | build
	$(OBJCOPY) -O binary $< $@

build/calc_data.o: build/calc.bin | build
	$(LD) -m elf_i386 -r -b binary -o $@ $<

# --- Kernel assembly ---

build/boot.o: boot/boot.asm | build
	$(ASM) -f elf32 $< -o $@

build/interrupts_asm.o: interrupts/interrupts.asm | build
	$(ASM) -f elf32 $< -o $@

build/exceptions_asm.o: cpu/exceptions.asm | build
	$(ASM) -f elf32 $< -o $@

build/gdt_asm.o: arch/gdt.asm | build
	$(ASM) -f elf32 $< -o $@

build/tss_asm.o: arch/tss.asm | build
	$(ASM) -f elf32 $< -o $@

build/switch_asm.o: task/switch.asm | build
	$(ASM) -f elf32 $< -o $@

build/syscall_asm.o: user/syscall.asm | build
	$(ASM) -f elf32 -I user $< -o $@

# --- Kernel C (flat build/ name from source basename) ---

build/kernel.o: kernel/kernel.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/terminal.o: kernel/terminal.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/log.o: kernel/log.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/keyboard.o: kernel/keyboard.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/timer.o: kernel/timer.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/interrupts.o: interrupts/interrupts.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/pic.o: interrupts/pic.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/shell.o: shell/shell.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/util.o: memory/util.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/memory_map.o: memory/memory_map.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/pmm.o: memory/pmm.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/paging.o: memory/paging.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/heap.o: memory/heap.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/gdt.o: arch/gdt.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/tss.o: arch/tss.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/exceptions.o: cpu/exceptions.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/process.o: task/process.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/block.o: block/block.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/ata.o: drivers/ata.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/serial.o: drivers/serial.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/device.o: drivers/device.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/pci.o: drivers/pci.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/mouse.o: drivers/mouse.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/tfs.o: fs/tfs.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/fs.o: fs/fs.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/fs_selftest.o: fs/fs_selftest.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/exec.o: user/exec.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/syscall.o: user/syscall.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/uaccess.o: user/uaccess.c | build
	$(CC) $(CFLAGS) -c $< -o $@

# --- Disk image (created once; survives normal rebuilds) ---

$(DISK): | build
	dd if=/dev/zero of=$@ bs=1M count=$(DISK_SIZE_MB)

disk-reset: | build
	dd if=/dev/zero of=$(DISK) bs=1M count=$(DISK_SIZE_MB)

$(KERNEL): $(ASM_OBJECTS) $(C_OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

$(ISO): $(KERNEL)
	mkdir -p build/isodir/boot/grub
	cp $(KERNEL) build/isodir/boot/kernel.bin
	cp grub.cfg build/isodir/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ build/isodir

iso: $(ISO)

run: $(KERNEL) $(DISK)
	$(QEMU) -kernel $(KERNEL) \
		-drive file=$(DISK),format=raw,if=ide,index=0,media=disk \
		-serial stdio

selftest-run:
	$(MAKE) clean
	$(MAKE) CFLAGS_EXTRA=-DTESTOS_SELFTEST $(KERNEL)
	$(MAKE) disk-reset
	$(QEMU) -kernel $(KERNEL) \
		-drive file=$(DISK),format=raw,if=ide,index=0,media=disk \
		-serial stdio

# Wipe objects/kernel but keep the persistent disk image.
clean:
	rm -f build/*.o build/*.elf build/*.bin build/*.iso
	rm -rf build/isodir
