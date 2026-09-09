#!/usr/bin/env bash
# UEFI QEMU boot with NVMe data disk (no legacy IDE data disk).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "${root}"

image=${UEFI_IMAGE:-build/testos-usb-local.img}
code=${OVMF_CODE:-}
vars=${OVMF_VARS:-build/ovmf-vars.fd}
data=${UEFI_DATA:-build/uefi-data.img}

if [[ -z "${code}" ]]; then
  if [[ -f "/c/Program Files/qemu/share/edk2-x86_64-code.fd" ]]; then
    code="/c/Program Files/qemu/share/edk2-x86_64-code.fd"
  elif [[ -f "build/ovmf/RELEASEX64_OVMF_CODE.fd" ]]; then
    code="build/ovmf/RELEASEX64_OVMF_CODE.fd"
  else
    echo "OVMF code firmware not found; set OVMF_CODE" >&2
    exit 1
  fi
fi

QEMU64=${QEMU64:-qemu-system-x86_64}
PYTHON=${PYTHON:-python3}

if [[ ! -f "${vars}" ]]; then
  cp "build/ovmf/RELEASEX64_OVMF_VARS.fd" "${vars}" 2>/dev/null || \
    cp "/usr/share/OVMF/OVMF_VARS_4M.fd" "${vars}" 2>/dev/null || \
    cp "/usr/share/OVMF/OVMF_VARS.fd" "${vars}" 2>/dev/null || \
    "${PYTHON}" "dev/ovmf_vars.py" "${code}" "${vars}"
fi

test -f "${image}" || { echo "missing ${image}" >&2; exit 1; }
if [[ ! -f "${data}" ]]; then
  "${PYTHON}" - "${data}" <<PY
import pathlib, sys
pathlib.Path(sys.argv[1]).write_bytes(b"\\x00" * (16 * 1024 * 1024))
print("created", sys.argv[1])
PY
fi

# USB HID interrupt IN is not wired yet; usb-kbd steals GUI keys from PS/2.
usb_hid_args=()
if [[ -n "${TESTOS_USB_HID:-}" ]]; then
  usb_hid_args+=(-device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0)
fi

exec "${QEMU64}" \
  -machine q35 -m 512M \
  -drive if=pflash,format=raw,readonly=on,file="${code}" \
  -drive if=pflash,format=raw,file="${vars}" \
  -drive file="${image}",format=raw,if=none,id=boot \
  -device ide-hd,drive=boot,bootindex=0 \
  -drive file="${data}",format=raw,if=none,id=data \
  -device nvme,serial=testos,drive=data \
  -device qemu-xhci,id=xhci \
  "${usb_hid_args[@]}" \
  -netdev user,id=net0,hostfwd=tcp::8080-:8080 \
  -device e1000e,netdev=net0 \
  -device bochs-display -vga none \
  -serial stdio
