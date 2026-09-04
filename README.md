# TestOS 0.9

TestOS is a small 32-bit x86 operating system written in C and assembly. It
boots under QEMU and includes a shell, multitasking, user programs, a simple
filesystem, and an IPv4 network stack.

## Features

- Multiboot startup, GDT, IDT, PIC, PIT, paging, and a kernel heap
- Preemptive kernel and user processes
- System calls and a small user C library
- Interactive shell with filesystem, process, mouse, and network commands
- ATA disk support and the TFS filesystem
- Intel E1000 support for QEMU
- Ethernet, ARP, IPv4, ICMP, UDP, and TCP
- Serial logging and filesystem self-tests

## Build and run

The Docker development environment is the easiest way to build TestOS without
installing the cross-compilation and QEMU tools on the host.

```bash
./dev/run-in-docker.sh --build
```

The command builds the image and opens a shell in the repository. Inside that
shell:

```bash
make
make run
```

For a visual session from Docker, use VNC. Leave the command running and
connect from macOS to `vnc://localhost:5900` with password `testos1`.

```bash
make run-vnc
```

## UEFI live USB image

TestOS also provides a raw, whole-device image for x86-64 UEFI removable media.
It uses the pinned Limine 12.6.0 `x86_64-efi` loader at the standard
`EFI/BOOT/BOOTX64.EFI` path, a GPT, and a FAT32 EFI System Partition. Limine
loads a native x86-64 Limine-protocol diagnostic payload. Its boot information,
framebuffer pointers, dimensions, pitch, and memory-map entries are all
validated as 64-bit values. Limine's active higher-half direct map includes the
complete framebuffer; the payload never uses VGA memory or VGA CRTC I/O.
No UEFI NVRAM entry, Windows file, installer, or internal disk is used.

Inside the documented Docker environment, create and validate the exact image:

```bash
make usb-image
make uefi-usb-test
```

The artifact is `build/testos-usb.img`. The build prints its byte size and
SHA-256 checksum. `uefi-usb-test` verifies the GPT and ESP file layout, then
boots that raw image (never with `-kernel`) through OVMF from a new disposable
UEFI variable store five times at 256 MiB and once at 512 MiB, without a NIC or
debug-only device. The image is deliberately not signed: Secure Boot may need
to be temporarily disabled in firmware. Do not reset TPM keys, clear Secure
Boot keys, change BitLocker, or change Windows boot settings.
The test also writes `build/uefi-validation.txt` with the source revision,
image checksum, OVMF/QEMU configuration, and the verified stage plus 64-bit
framebuffer address for every cold boot.

The payload requests a 1024x768 32-bit framebuffer and renders stable stages
through `TESTOS READY`; serial logging is retained for QEMU. `uefi-usb-test`
uses a Bochs GOP device with `-vga none`, captures the framebuffer, and verifies the
same raw image (never `-kernel`). On a laptop, the `TESTOS READY` screen is the
success marker. This is an early x86-64 UEFI display layer: the existing
32-bit kernel continues to supply the separate legacy QEMU/TCP/socket/HTTP
development paths and is not entered by the UEFI diagnostic image.

### UEFI framebuffer architecture and limits

`build/testos-uefi.elf` is an ELF64 x86-64 System V ABI payload. Limine enters
its `_start` with its paging and descriptor state already active; the first two
instructions execute `cli` and `cld` before C code runs. The payload validates
the HHDM, memory-map, and framebuffer response revisions, bounded response
counts, every memory-map range, pixel masks, pitch, framebuffer size overflow,
and that the derived 64-bit framebuffer physical range is contained in Limine's
framebuffer-reserved map entry. It writes through Limine's 64-bit HHDM mapping;
no address is converted to a 32-bit pointer. Invalid data produces a stable
serial error (`E001`–`E003`) and halts without storage writes.

After display validation it masks both PICs and programs PIT channel 0 at
100 Hz, while retaining `cli`; this makes the timer stage a real initialized
state without permitting unhandled interrupts in this deliberately minimal
diagnostic layer. The supplied QEMU Bochs GOP configuration reports a
framebuffer at `0x0000000080000000`; its device aperture is capped below 4 GiB,
so it cannot exercise an above-4-GiB framebuffer. The payload itself carries
and validates the derived address as a 64-bit value, ready for firmware that
places GOP memory above that boundary.

Limine 12.6.0 and its release-asset SHA-256 are pinned in `dev/Dockerfile`.
The UEFI diagnostic layer is deliberately limited to the safe boot display
milestone; it does not yet transition into the legacy 32-bit kernel or expose
its storage/network stack on physical UEFI hardware.
Physical hardware support is limited to this boot milestone: networking,
internal storage access, battery support, sound, and sleep are not required.
Do not mount, format, or otherwise write any internal storage from TestOS.

### Flash a spare USB only after explicit inspection

Flashing destroys the target drive's partition table and all files. The normal
build command never flashes a device. On macOS, first inspect only removable
physical media and confirm the model, capacity, partitions, and exact disk
identifier. Do not substitute a guess or a partition identifier.

```bash
diskutil list external physical
# After confirming the exact removable whole disk, for example /dev/diskN:
diskutil unmountDisk /dev/diskN
sudo dd if=build/testos-usb.img of=/dev/rdiskN bs=4m status=progress
sync
diskutil eject /dev/diskN
```

Before running `dd`, compare `shasum -a 256 build/testos-usb.img` with the
build output. Afterward, where practical, compare image regions from the USB
against the source image. On Windows, use Rufus in DD/image mode or
balenaEtcher, select the generated image and the removable USB carefully, and
never select the internal Windows disk.

To test, fully shut down Windows, insert the USB, open the one-time boot menu,
and choose the USB's UEFI entry. If necessary, temporarily disable Secure Boot
only, retaining the Windows recovery key. Record the last visible boot stage.
Power off, remove the USB, and confirm Windows starts normally; TestOS must not
have modified its internal disk.

You can choose another temporary VNC password with
`make run-vnc VNC_PASSWORD=yourpass`.

## Networking

QEMU user networking gives the guest address `10.0.2.15` and gateway
`10.0.2.2`.

- Host UDP `127.0.0.1:12345` forwards to the guest UDP echo service.
- Host TCP `127.0.0.1:12346` forwards to the guest TCP echo service.
- Host TCP `127.0.0.1:8080` forwards to the guest HTTP service. Open
  `http://127.0.0.1:8080/` in Safari while `make run` is active through Docker.

The shell includes `ping`, `udp`, and `netrx` commands. The TCP service accepts
standard host TCP connections and echoes application data.

## TCP regression test

Inside the Docker shell, run:

```bash
./dev/tcp-interop-test.sh
```

The test builds a test kernel, boots QEMU headlessly, checks active and passive
opens, exercises malformed packets and retransmission, exchanges 25 sequential
1021-byte payloads with a standard host TCP peer, and validates a PCAP. It
leaves `build/tcp-serial.log` and `build/tcp.pcap` for inspection, then rebuilds
the normal kernel.

The TCP implementation is intentionally small: IPv4 only, eight connection
slots, a 1024-byte receive window, one unacknowledged application segment per
connection, and no TCP option negotiation, congestion control, fragmentation
reassembly, or TIME-WAIT state.

## Kernel sockets and HTTP

`net/socket.h` exposes a bounded kernel API for create, bind, listen, accept,
connect, send, receive, and close. Handles include a generation counter, so
stale handles are rejected. Send buffers remain caller-owned; `socket_send`
returns `SOCKET_WOULD_BLOCK` while its prior TCP segment awaits acknowledgement.
Receive returns buffered bytes before `SOCKET_EOF` after a peer close.
There are eight socket descriptors, a four-connection listener backlog, and a
2048-byte receive buffer per connection. Calls never sleep: the timer polling
path advances TCP and HTTP, so applications retry `SOCKET_WOULD_BLOCK`.

The HTTP/1.0 service listens on guest port 8080 and uses only this socket API.
It handles `GET /`, 404, 405, malformed input, and bounded headers. Its
1400-byte page deliberately spans multiple TCP segments.

Inside the Docker shell, run the headless verification:

```bash
./dev/http-interop-test.sh
```

It builds and boots QEMU, waits for the HTTP-ready serial message, validates
framing and body length, fragmentation, errors, reuse, four concurrent clients,
and leaves `build/http-serial.log` plus `build/http.pcap` as evidence.

## Filesystem checks

```bash
make selftest-run
```

The shell also provides `fsck` and `fstest` commands.

## Layout

| Directory | Contents |
| --- | --- |
| `boot` | Multiboot entry code |
| `kernel`, `arch`, `interrupts`, `cpu` | Kernel startup and hardware support |
| `memory`, `task` | Memory management and processes |
| `drivers` | ATA, PCI, serial, mouse, and E1000 drivers |
| `net` | Ethernet, ARP, IPv4, ICMP, UDP, and TCP |
| `fs`, `block` | Filesystem and block-device layers |
| `user`, `shell` | User programs, system calls, and shell |
| `dev` | Docker environment and regression scripts |

## Version

Current release: **0.9**.

## License

All rights reserved unless otherwise noted. Personal / portfolio project.
