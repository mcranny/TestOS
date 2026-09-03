#!/usr/bin/env bash
set -euo pipefail
make clean
make all build/disk.img
mkdir -p build
rm -f build/http-serial.log build/http.pcap
qemu-system-i386 -kernel build/kernel.bin \
  -drive file=build/disk.img,format=raw,if=ide,index=0,media=disk \
  -netdev user,id=net0,hostfwd=tcp::8080-:8080 \
  -object filter-dump,id=httpdump,netdev=net0,file=build/http.pcap \
  -device e1000,netdev=net0 -display none -serial file:build/http-serial.log -monitor none &
qemu_pid=$!
cleanup() { kill "${qemu_pid}" 2>/dev/null || true; wait "${qemu_pid}" 2>/dev/null || true; }
trap cleanup EXIT
python3 dev/http-interop-test.py
grep -q 'HTTP: Ready on port 8080' build/http-serial.log
cleanup
trap - EXIT
test -s build/http.pcap
