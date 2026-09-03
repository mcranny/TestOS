# TestOS 0.8.1

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

Current release: **0.8.1**.

## License

All rights reserved unless otherwise noted. Personal / portfolio project.
