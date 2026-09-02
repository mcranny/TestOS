# TestOS 0.7.0

Hobby x86 operating system written in C and assembly. Boots under QEMU with a shell, preemptive multitasking, user-mode programs, a persistent disk filesystem, and a minimal IPv4 network stack.

## Features

- Multiboot kernel with GDT, IDT, PIC, and PIT timer
- Physical memory manager, paging, and kernel heap (with allocation canaries)
- Preemptive round-robin scheduler and kernel/user processes with safe reaping
- Kernel stack canaries and process lifecycle validation
- System calls (`int 0x80`) and a small user C library
- Interactive shell (`ls`, `cat`, `write`, `cp`, `mv`, `ps`, `kill`, `mouse`, `ping`, `udp`, `netrx`, `./calc`, `fsck`, `fstest`, …)
- Block-device layer with ATA PIO driver
- Custom on-disk filesystem (TFS) with `tfs_check` consistency pass
- PCI enumeration and device registry (BAR decode, bus mastering)
- Intel E1000 NIC driver (MMIO, TX/RX rings) under QEMU
- Network stack: Ethernet, ARP, IPv4, ICMP ping, UDP with port bind/echo
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

### Networking

`make run` attaches a user-mode NIC with UDP port forward `host:12345` → guest `:12345` (UDP echo service).

In the guest shell:

- `ping 10.0.2.2` — ICMP echo to the QEMU gateway
- `udp <ip> <port> <message>` — send a UDP datagram
- `netrx` — poll for received frames

From the host, send UDP to `127.0.0.1:12345` to exercise the guest echo service.

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
| `drivers/` | Hardware drivers (ATA, serial, PCI, E1000, mouse) |
| `net/` | Ethernet, ARP, IPv4, ICMP, UDP |
| `shell/` | Interactive shell |
| `cpu/` / `arch/` / `interrupts/` | Exceptions, GDT/TSS, IRQs |

## Version

Current release: **0.7.0** (see `include/version.h`).

## License

All rights reserved unless otherwise noted. Personal / portfolio project.
