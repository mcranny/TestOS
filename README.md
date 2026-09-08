# TestOS

x86-64 operating system with UEFI boot. Runs under QEMU or from USB.

## Features

- Console and serial output
- Preemptive multitasking and user-mode programs
- Interactive shell
- Persistent filesystem on disk

## Planned

- Networking
- Additional user programs

## Run

```bash
make run-uefi
```

Windows (clang + QEMU): `powershell -File dev/build-uefi.ps1`, then `./dev/run-uefi.sh`.

USB image: `make usb-image` (see `dev/`).

## Layout

- `uefi/` — kernel
- `dev/` — build and run helpers
- `legacy/` — archived 32-bit tree
