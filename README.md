 TestOS 0.10

Small x86-64 OS. Boots via Limine on UEFI (QEMU or USB).

## Now

- Limine UEFI boot, framebuffer console, serial log
- GDT / IDT / TSS, PIC, PIT, keyboard
- PMM, 4-level paging, kernel heap
- PCI scan, ATA PIO + TFS (second IDE disk in QEMU)
- Preemptive processes, syscalls (`syscall`/`sysret`), ring 3
- ELF64 loader, shell, `calc`, `ps` / `kill`

## Next

- Networking (E1000 / IPv4 stack port)
- More user programs and shell polish

## Run

```bash
make run-uefi
```

Windows (clang + QEMU): `powershell -File dev/build-uefi.ps1`, refresh the local USB image, then `./dev/run-uefi.sh`.

USB image for real hardware: `make usb-image` (see `dev/`).

## Layout

- `uefi/` — active kernel
- `dev/` — build / QEMU / image helpers
- `legacy/` — archived 32-bit Multiboot tree (not the default build)
