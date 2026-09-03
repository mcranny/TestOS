Dev container for building and running TestOS

Purpose
- Provide an isolated Debian container with all required build tools and QEMU so the host macOS stays clean.

Quick start

1. Build the dev image (on mac):

   ```bash
   ./dev/run-in-docker.sh --build
   ```

2. Start a shell in the container (mounts repo at `/work`):

   ```bash
   ./dev/run-in-docker.sh
   ```

3. Inside the container run the usual build/run commands:

   ```bash
   make
   make iso
   make run

   # Headless TCP host/guest interoperability test (active and passive open,
   # 25 sequential host-to-guest echoes, malformed input, retransmission, PCAP)
   ./dev/tcp-interop-test.sh

   # Visual boot from Docker: run this, then connect macOS Screen Sharing to
   # vnc://localhost:5900 using password testos1.
   make run-vnc
   ```

Notes
- The launch script builds an AMD64 container so its 32-bit multilib toolchain
  is available on Apple Silicon hosts. It includes `nasm`, `gcc` (multilib), `binutils`,
  `grub-mkrescue`, `qemu-system-i386`, and Python for the TCP test client.
- If your Makefile still points to a host-specific `TOOLCHAIN` or `QEMU` path, override environment variables when invoking `make`, for example:

  ```bash
  make TOOLCHAIN= CC=gcc LD=ld QEMU=qemu-system-i386 GRUB_MKRESCUE=grub-mkrescue
  ```

Security & cleanliness
- The container runs ephemeral; it mounts your repo read/write at `/work` only. The host system packages are untouched.
- QEMU runs inside the container, so no global installs on macOS are required.
