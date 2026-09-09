# TestOS

x86-64 operating system with UEFI boot. Runs under QEMU or from USB.

## Features

- Console, framebuffer, and serial I/O
- Preemptive multitasking and user-mode programs
- Interactive shell
- Persistent TFS filesystem (512-byte sectors)
- Block storage backends: ATA PIO (`hd0`), AHCI/SATA (`sata0`), NVMe (`nvme0`)
- Networking: E1000e, ARP/IPv4/ICMP/UDP/TCP, HTTP on port 8080
- xHCI controller bring-up; PS/2 keyboard remains the interactive input path
- GitHub Actions smoke checks for ATA, AHCI, and NVMe

## Run

Default ATA data disk:

```bash
make run-uefi
```

Exclusive storage backends:

```bash
./dev/run-uefi-ahci.sh
./dev/run-uefi-nvme.sh
```

Windows (clang + QEMU): `powershell -File dev/build-uefi.ps1`, then `./dev/run-uefi.sh`.

USB image: `make usb-image` (see `dev/`). Refresh the local image after rebuilds with `python dev/refresh-usb-local.py` when using `build/testos-usb-local.img`.

Optional USB HID devices for enumeration experiments (GUI typing still uses PS/2 by default):

```bash
TESTOS_USB_HID=1 ./dev/run-uefi.sh
```

## Smoke tests

```bash
python dev/uefi-shell-smoke.py ata
python dev/uefi-shell-smoke.py ahci
python dev/uefi-shell-smoke.py nvme
python dev/uefi-http-smoke.py
```

## Layout

- `uefi/` — kernel (drivers, FS, net, USB, shell)
- `dev/` — build, run, and smoke helpers
- `legacy/` — archived 32-bit tree
- `.github/workflows/` — UEFI PR checks
