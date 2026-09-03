# Platform detection (default to macOS when building on Darwin)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    HOST_OS := mac
else
    HOST_OS := linux
endif

# Allow environment overrides; auto-detect common cross-toolchain if available.
TOOLCHAIN ?=
ASM ?= nasm

# Prefer an explicit i686-elf cross-toolchain when available in PATH, otherwise
# fall back to host `gcc`/`ld` (requires multilib). When using the host gcc,
# add `-m32` so code and assembly are compiled for i386.
HAS_I686 := $(shell command -v i686-elf-gcc 2>/dev/null || true)
ifeq ($(HAS_I686),)
    CC := gcc -m32
    LD := ld
    OBJCOPY := objcopy
    CFLAGS := $(CFLAGS) -m32
    USER_CFLAGS := $(USER_CFLAGS) -m32
    USER_LDFLAGS := $(USER_LDFLAGS) -m elf_i386
else
    CC ?= i686-elf-gcc
    LD ?= i686-elf-ld
    OBJCOPY ?= i686-elf-objcopy
endif

# Allow overriding grub and qemu; pick reasonable defaults for macOS hosts.
GRUB_MKRESCUE ?= grub-mkrescue
HAS_QEMU := $(shell command -v qemu-system-i386 2>/dev/null || true)
ifeq ($(HAS_QEMU),)
    ifeq ($(HOST_OS),mac)
        QEMU ?= qemu-system-i386
    else
        QEMU ?= "/c/Program Files/qemu/qemu-system-i386.exe"
    endif
else
    # Use absolute path to qemu-system-i386 to avoid ambiguous invocation
    QEMU ?= $(HAS_QEMU)
endif

# Display options: default to a larger Cocoa window on macOS. You can override
# `QEMU_DISPLAY_OPTS` in the environment or on the `make` command line.
ifeq ($(HOST_OS),mac)
    QEMU_DISPLAY_OPTS ?= -display cocoa -vga std
else
    QEMU_DISPLAY_OPTS ?= -display sdl -vga std
endif

KERNEL = build/kernel.bin
ISO    = build/testos.iso
DISK   = build/disk.img
DISK_SIZE_MB = 16
VNC_PASSWORD ?= testos1

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
         -Inet \
         $(CFLAGS_EXTRA)

LDFLAGS = -m elf_i386 -T linker.ld

ifeq ($(findstring i686-elf,$(CC)),i686-elf)
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
else
USER_CFLAGS = -ffreestanding \
              -fno-pie \
              -fno-stack-protector \
              -fno-omit-frame-pointer \
              -fno-asynchronous-unwind-tables \
              -nostdlib \
              -nostdinc \
              -Wall \
              -Wextra \
              -Iinclude \
              -Iuser
endif

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
    drivers/e1000.c \
    drivers/mouse.c \
    net/mac.c \
    net/ethernet.c \
    net/arp.c \
    net/ipv4.c \
    net/icmp.c \
    net/checksum.c \
    net/tcp.c \
    net/socket.c \
    net/http.c \
    net/udp.c \
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

.PHONY: all iso clean run run-vnc disk-reset selftest-run

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

build/e1000.o: drivers/e1000.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/mouse.o: drivers/mouse.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/mac.o: net/mac.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/ethernet.o: net/ethernet.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/arp.o: net/arp.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/ipv4.o: net/ipv4.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/icmp.o: net/icmp.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/checksum.o: net/checksum.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/udp.o: net/udp.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/tcp.o: net/tcp.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/socket.o: net/socket.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/http.o: net/http.c | build
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
		-netdev user,id=net0,hostfwd=udp::12345-:12345,hostfwd=tcp::12346-:12346,hostfwd=tcp::8080-:8080 -device e1000,netdev=net0 \
		$(QEMU_DISPLAY_OPTS) \
		-serial stdio

# Docker-friendly visual boot: connect from the host to VNC port 5900 using
# the temporary password in VNC_PASSWORD (default: testos1).
run-vnc: $(KERNEL) $(DISK)
	$(QEMU) -kernel $(KERNEL) \
		-drive file=$(DISK),format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0,hostfwd=udp::12345-:12345,hostfwd=tcp::12346-:12346,hostfwd=tcp::8080-:8080 -device e1000,netdev=net0 \
		-object secret,id=vncpass,data=$(VNC_PASSWORD),format=raw \
		-display none -vnc 0.0.0.0:0,password-secret=vncpass \
		-serial stdio

selftest-run:
	$(MAKE) clean
	$(MAKE) CFLAGS_EXTRA=-DTESTOS_SELFTEST $(KERNEL)
	$(MAKE) disk-reset
	$(QEMU) -kernel $(KERNEL) \
		-drive file=$(DISK),format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0,hostfwd=udp::12345-:12345,hostfwd=tcp::12346-:12346 -device e1000,netdev=net0 \
		$(QEMU_DISPLAY_OPTS) \
		-serial stdio

# Wipe objects/kernel but keep the persistent disk image.
clean:
	rm -f build/*.o build/*.elf build/*.bin build/*.iso
	rm -rf build/isodir
