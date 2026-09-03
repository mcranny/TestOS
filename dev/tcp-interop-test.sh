#!/usr/bin/env bash
set -euo pipefail

# Run from the repository root, inside the documented dev container.
make clean
make CFLAGS_EXTRA='-DTESTOS_TCP_ACTIVE_TEST -DTESTOS_TCP_TEST_HOOKS -DTESTOS_TCP_SELFTEST' all build/disk.img
mkdir -p build
rm -f build/tcp-serial.log build/tcp.pcap
python3 -c 'import socket; s=socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(("0.0.0.0", 12347)); s.listen(1); c, _ = s.accept(); c.close(); s.close()' &
peer_pid=$!
qemu-system-i386 -kernel build/kernel.bin \
  -drive file=build/disk.img,format=raw,if=ide,index=0,media=disk \
  -netdev user,id=net0,hostfwd=tcp::12346-:12346 \
  -object filter-dump,id=tcpdump,netdev=net0,file=build/tcp.pcap \
  -device e1000,netdev=net0 \
  -display none -serial file:build/tcp-serial.log -monitor none &
qemu_pid=$!
cleanup() {
  kill "${qemu_pid}" "${peer_pid}" 2>/dev/null || true
  wait "${qemu_pid}" "${peer_pid}" 2>/dev/null || true
}
trap cleanup EXIT

python3 dev/tcp-interop-test.py
grep -q 'TCP: Active open established' build/tcp-serial.log
test "$(grep -c 'TCP: Retransmitted segment' build/tcp-serial.log)" -ge 2
grep -q 'TCP: Selftest passed' build/tcp-serial.log
cleanup
trap - EXIT
python3 dev/tcp-pcap-check.py build/tcp.pcap
make clean
make all
