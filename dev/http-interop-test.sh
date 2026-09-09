#!/usr/bin/env bash
# LEGACY (i386 Multiboot) — do not use for v0.13+ CI.
#
# Previously booted qemu-system-i386 + kernel.bin for host HTTP interop.
# UEFI replacement:
#   python3 dev/uefi-http-smoke.py
#   python3 dev/uefi-net-smoke.py   # also runs http-interop-test.py
set -euo pipefail
echo "dev/http-interop-test.sh is legacy (i386). Use: python3 dev/uefi-http-smoke.py" >&2
exit 2
