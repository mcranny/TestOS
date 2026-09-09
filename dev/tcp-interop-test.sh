#!/usr/bin/env bash
# LEGACY (i386 Multiboot) — do not use for v0.13+ CI.
#
# This script previously rebuilt the deleted qemu-system-i386 kernel with
# TESTOS_TCP_SELFTEST / TESTOS_TCP_ACTIVE_TEST hooks, ran host TCP echo, and
# checked a filter-dump PCAP. That tree lives under legacy/ only.
#
# UEFI replacement (wired in .github/workflows/uefi-pr.yml via uefi-net-smoke):
#   python3 dev/uefi-net-smoke.py
# which runs tcp-interop-test.py + tcp-pcap-check.py against the UEFI USB image.
#
# Kernel compile-time TCP selftest / retransmit hooks are intentionally not
# claimed by UEFI CI.
set -euo pipefail
echo "dev/tcp-interop-test.sh is legacy (i386). Use: python3 dev/uefi-net-smoke.py" >&2
exit 2
