# TestOS 0.8.0

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

You can choose another temporary VNC password with
`make run-vnc VNC_PASSWORD=yourpass`.

## Networking

QEMU user networking gives the guest address `10.0.2.15` and gateway
`10.0.2.2`.

- Host UDP `127.0.0.1:12345` forwards to the guest UDP echo service.
- Host TCP `127.0.0.1:12346` forwards to the guest TCP echo service.

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

Current release: **0.8.0**.

## License

All rights reserved unless otherwise noted. Personal / portfolio project.
