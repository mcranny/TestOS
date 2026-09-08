# TestOS — x86-64 UEFI (Limine) kernel
# Legacy 32-bit Multiboot tree: see legacy/

UEFI_KERNEL = build/testos-uefi.elf
USB_IMAGE = build/testos-usb.img
USB_LOCAL_IMAGE = build/testos-usb-local.img

UEFI_CLANG := $(shell command -v clang 2>/dev/null || true)
ifeq ($(UEFI_CLANG),)
    ifneq ($(wildcard /c/Program\ Files/LLVM/bin/clang.exe),)
        UEFI_CLANG := /c/Program Files/LLVM/bin/clang.exe
        UEFI_LD := /c/Program Files/LLVM/bin/ld.lld.exe
    else ifneq ($(wildcard C:/Program\ Files/LLVM/bin/clang.exe),)
        UEFI_CLANG := C:/Program Files/LLVM/bin/clang.exe
        UEFI_LD := C:/Program Files/LLVM/bin/ld.lld.exe
    else
        UEFI_CLANG := clang
        UEFI_LD := ld.lld
    endif
else
    UEFI_LD ?= $(shell command -v ld.lld 2>/dev/null || echo ld.lld)
endif

PYTHON ?= $(shell \
	command -v python3 2>/dev/null || \
	command -v python 2>/dev/null || \
	command -v py 2>/dev/null || \
	echo python)

UEFI_CFLAGS = --target=x86_64-unknown-none-elf -m64 -ffreestanding -fno-pie -fno-pic \
	-fno-stack-protector -mno-red-zone -mcmodel=kernel -nostdlib -nostdinc \
	-Wall -Wextra -Iuefi

UEFI_C_SOURCES = \
	uefi/kernel.c \
	uefi/boot/limine_boot.c \
	uefi/drivers/serial.c \
	uefi/drivers/log.c \
	uefi/drivers/fb.c \
	uefi/drivers/pic.c \
	uefi/drivers/pit.c \
	uefi/arch/tss.c \
	uefi/arch/gdt.c \
	uefi/arch/idt.c \
	uefi/arch/interrupts.c \
	uefi/mm/pmm.c \
	uefi/mm/paging.c \
	uefi/mm/heap.c \
	uefi/lib/string.c \
	uefi/drivers/kbd.c \
	uefi/drivers/console.c \
	uefi/drivers/device.c \
	uefi/drivers/pci.c \
	uefi/drivers/ata.c \
	uefi/block/block.c \
	uefi/fs/tfs.c \
	uefi/fs/path.c \
	uefi/fs/ramfs.c \
	uefi/task/process.c \
	uefi/user/syscall.c \
	uefi/user/elf64.c \
	uefi/user/exec.c \
	uefi/shell/shell.c

UEFI_S_SOURCES = \
	uefi/entry.S \
	uefi/arch/isr.S \
	uefi/arch/gdt_load.S \
	uefi/task/switch.S \
	uefi/user/syscall_entry.S \
	uefi/user/calc_blob.S

UEFI_C_OBJECTS = $(patsubst uefi/%.c,build/uefi/%.o,$(UEFI_C_SOURCES))
UEFI_S_OBJECTS = $(patsubst uefi/%.S,build/uefi/%.o,$(UEFI_S_SOURCES))
UEFI_OBJECTS = $(UEFI_S_OBJECTS) $(UEFI_C_OBJECTS)

.PHONY: all uefi-kernel usb-image usb-image-local uefi-usb-test run run-uefi clean

all: uefi-kernel

build:
	mkdir -p build

uefi-kernel: $(UEFI_KERNEL)

build/uefi/%.o: uefi/%.c uefi/limine.h uefi/types.h uefi/platform.h | build
	mkdir -p $(dir $@)
	"$(UEFI_CLANG)" $(UEFI_CFLAGS) -c $< -o $@

build/uefi/%.o: uefi/%.S | build
	mkdir -p $(dir $@)
	"$(UEFI_CLANG)" $(UEFI_CFLAGS) -c $< -o $@

$(UEFI_KERNEL): $(UEFI_OBJECTS) uefi/linker.ld build/uefi-calc.elf
	"$(UEFI_LD)" -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -T uefi/linker.ld -o $@ $(UEFI_OBJECTS)

build/uefi-calc.elf: uefi/user/crt0.S uefi/user/ulib.c uefi/user/calc.c uefi/user/user.ld | build
	mkdir -p build/uefi/user
	"$(UEFI_CLANG)" $(UEFI_CFLAGS) -Iuefi/user -c uefi/user/crt0.S -o build/uefi/user/crt0.o
	"$(UEFI_CLANG)" $(UEFI_CFLAGS) -Iuefi/user -c uefi/user/ulib.c -o build/uefi/user/ulib.o
	"$(UEFI_CLANG)" $(UEFI_CFLAGS) -Iuefi/user -c uefi/user/calc.c -o build/uefi/user/calc.o
	"$(UEFI_LD)" -m elf_x86_64 -nostdlib -T uefi/user/user.ld -o $@ build/uefi/user/crt0.o build/uefi/user/ulib.o build/uefi/user/calc.o

build/uefi/user/calc_blob.o: uefi/user/calc_blob.S build/uefi-calc.elf | build
	mkdir -p $(dir $@)
	"$(UEFI_CLANG)" $(UEFI_CFLAGS) -c $< -o $@

usb-image: $(UEFI_KERNEL)
	./dev/build-usb-image.sh

usb-image-local: $(UEFI_KERNEL)
	@if [ -f $(USB_LOCAL_IMAGE) ]; then \
		"$(PYTHON)" dev/refresh-usb-local.py; \
	else \
		"$(PYTHON)" dev/make-local-usb-image.py; \
	fi

uefi-usb-test: usb-image
	./dev/uefi-usb-test.sh $(USB_IMAGE)

run: run-uefi

run-uefi: usb-image-local
	./dev/run-uefi.sh

clean:
	rm -rf build/uefi build/testos-uefi.elf build/uefi-calc.elf
	rm -f build/*.log build/*.ppm
