# TestOS 0.6.0

Hobby x86 operating system written in C and assembly. Boots under QEMU with a shell, preemptive multitasking, user-mode programs, and a persistent disk filesystem.

## Features

- Multiboot kernel with GDT, IDT, PIC, and PIT timer
- Physical memory manager, paging, and kernel heap (with allocation canaries)
- Preemptive round-robin scheduler and kernel/user processes with safe reaping
- Kernel stack canaries and process lifecycle validation
- System calls (`int 0x80`) and a small user C library
- Interactive shell (`ls`, `cat`, `write`, `cp`, `mv`, `ps`, `kill`, `mouse`, `./calc`, `fsck`, `fstest`, …)
- Block-device layer with ATA PIO driver
- Custom on-disk filesystem (TFS) with `tfs_check` consistency pass
- PCI enumeration and device registry
- PS/2 mouse (IRQ12) with shell `mouse` status
- COM1 serial debug output and unified `klog` / `panic`
- User-mode calculator loaded from the filesystem

## Build

Requires:

- [nasm](https://www.nasm.us/)
- `i686-elf-gcc` / `i686-elf-ld` (or adjust `TOOLCHAIN` in the Makefile)
- QEMU (`qemu-system-i386`)
- MSYS2 or similar Unix-like environment on Windows helps for `make` / `dd`

```bash
make        # build kernel
make run    # boot in QEMU with IDE disk (build/disk.img) and COM1 on stdio
```

First run creates a 16 MiB `build/disk.img` if missing. To wipe the disk and re-seed `/calc` + `readme.txt`:

```bash
make disk-reset
```

`make clean` removes objects and the kernel binary but keeps `disk.img`.

### Serial console

`make run` passes `-serial stdio`, so boot and kernel logs appear on the host terminal as well as VGA.

### Filesystem checks

In the shell:

- `fsck` — run `tfs_check` and report OK/FAILED
- `fstest` — stress create/write/read/delete/fill, then `tfs_check`

Boot-time self-test (optional):

```bash
make selftest-run
# or: make CFLAGS_EXTRA=-DTESTOS_SELFTEST && make run
```

Persistence across reboot is still a manual check: write a file, quit QEMU, `make run` again, and `cat` the file.

### Debug categories

`kernel/log.h` gates `KLOG_DEBUG` per subsystem (`DEBUG_MEM`, `DEBUG_PROC`, `DEBUG_FS`, …). INFO and above always emit to VGA and serial.

## Layout

| Path | Role |
|------|------|
| `boot/` | Multiboot entry |
| `kernel/` | Core kernel, terminal, keyboard, timer, logging |
| `memory/` | PMM, paging, heap |
| `task/` | Processes and context switch |
| `user/` | Syscalls, exec, `calc` |
| `fs/` | VFS API + TFS + selftest |
| `block/` | Block device abstraction |
| `drivers/` | Hardware drivers (ATA, serial, PCI, mouse) |
| `shell/` | Interactive shell |
| `cpu/` / `arch/` / `interrupts/` | Exceptions, GDT/TSS, IRQs |

## Version

Current release: **0.6.0** (see `include/version.h`).

## License

All rights reserved unless otherwise noted. Personal / portfolio project.
