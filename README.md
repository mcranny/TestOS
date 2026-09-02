# TestOS 0.5.0

Hobby x86 operating system written in C and assembly. Boots under QEMU with a shell, preemptive multitasking, user-mode programs, and a persistent disk filesystem.

## Features

- Multiboot kernel with GDT, IDT, PIC, and PIT timer
- Physical memory manager, paging, and kernel heap
- Preemptive round-robin scheduler and kernel/user processes
- System calls (`int 0x80`) and a small user C library
- Interactive shell (`ls`, `cat`, `write`, `ps`, `kill`, `./calc`, …)
- Block-device layer with ATA PIO driver
- Custom on-disk filesystem (TFS) backed by a QEMU raw disk image
- User-mode calculator loaded from the filesystem

## Build

Requires:

- [nasm](https://www.nasm.us/)
- `i686-elf-gcc` / `i686-elf-ld` (or adjust `TOOLCHAIN` in the Makefile)
- QEMU (`qemu-system-i386`)
- MSYS2 or similar Unix-like environment on Windows helps for `make` / `dd`

```bash
make        # build kernel
make run    # boot in QEMU with IDE disk (build/disk.img)
```

First run creates a 16 MiB `build/disk.img` if missing. To wipe the disk and re-seed `/calc` + `readme.txt`:

```bash
make disk-reset
```

`make clean` removes objects and the kernel binary but keeps `disk.img`.

## Layout

| Path | Role |
|------|------|
| `boot/` | Multiboot entry |
| `kernel/` | Core kernel, terminal, keyboard, timer |
| `memory/` | PMM, paging, heap |
| `task/` | Processes and context switch |
| `user/` | Syscalls, exec, `calc` |
| `fs/` | VFS API + TFS |
| `block/` | Block device abstraction |
| `drivers/` | Hardware drivers (ATA today) |
| `shell/` | Interactive shell |
| `cpu/` / `arch/` / `interrupts/` | Exceptions, GDT/TSS, IRQs |

## Version

Current release: **0.5.0** (see `include/version.h`).

## License

All rights reserved unless otherwise noted. Personal / portfolio project.
