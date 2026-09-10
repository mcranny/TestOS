#!/usr/bin/env bash
# Interactive UEFI QEMU boot using the local GPT/FAT image.
# Boot disk: q35 AHCI (Limine). Data disk: classic ISA IDE at 0x1F0 for ATA PIO + TFS.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "${root}"

image=${UEFI_IMAGE:-build/testos-usb-local.img}
code=${OVMF_CODE:-}
vars=${OVMF_VARS:-build/ovmf-vars.fd}

if [[ -z "${code}" ]]; then
  if [[ -f "/c/Program Files/qemu/share/edk2-x86_64-code.fd" ]]; then
    code="/c/Program Files/qemu/share/edk2-x86_64-code.fd"
  elif [[ -f "build/ovmf/RELEASEX64_OVMF_CODE.fd" ]]; then
    code="build/ovmf/RELEASEX64_OVMF_CODE.fd"
  elif [[ -f "/usr/share/OVMF/OVMF_CODE_4M.fd" ]]; then
    code="/usr/share/OVMF/OVMF_CODE_4M.fd"
  else
    echo "OVMF code firmware not found; set OVMF_CODE" >&2
    exit 1
  fi
fi

QEMU64=${QEMU64:-}
if [[ -z "${QEMU64}" ]]; then
  if command -v qemu-system-x86_64 >/dev/null 2>&1; then
    QEMU64=$(command -v qemu-system-x86_64)
  elif [[ -x "/c/Program Files/qemu/qemu-system-x86_64.exe" ]]; then
    QEMU64="/c/Program Files/qemu/qemu-system-x86_64.exe"
  else
    echo "qemu-system-x86_64 not found; set QEMU64" >&2
    exit 1
  fi
fi

PYTHON=${PYTHON:-}
if [[ -z "${PYTHON}" ]]; then
  if command -v python3 >/dev/null 2>&1; then
    PYTHON=$(command -v python3)
  elif command -v python >/dev/null 2>&1; then
    PYTHON=$(command -v python)
  elif command -v py >/dev/null 2>&1; then
    PYTHON=$(command -v py)
  else
    for candidate in \
      "/c/Users/${USER}/AppData/Local/Programs/Python/Python311/python.exe" \
      "/c/Users/${USERNAME}/AppData/Local/Programs/Python/Python311/python.exe" \
      /c/Users/*/AppData/Local/Programs/Python/Python3*/python.exe
    do
      if [[ -x "${candidate}" ]]; then
        PYTHON=${candidate}
        break
      fi
    done
  fi
fi
if [[ -z "${PYTHON}" ]]; then
  echo "Python not found; set PYTHON to your python.exe" >&2
  exit 1
fi

if [[ ! -f "${vars}" ]]; then
  if [[ -f "build/ovmf/RELEASEX64_OVMF_VARS.fd" ]]; then
    cp "build/ovmf/RELEASEX64_OVMF_VARS.fd" "${vars}"
  elif [[ -f "/usr/share/OVMF/OVMF_VARS_4M.fd" ]]; then
    cp "/usr/share/OVMF/OVMF_VARS_4M.fd" "${vars}"
  elif [[ -f "/usr/share/OVMF/OVMF_VARS.fd" ]]; then
    cp "/usr/share/OVMF/OVMF_VARS.fd" "${vars}"
  else
    # Prefer a real VARS template via helper; blank classic size is 256 KiB.
    "${PYTHON}" "dev/ovmf_vars.py" "${code}" "${vars}"
  fi
fi

test -f "${image}" || { echo "missing ${image}" >&2; exit 1; }

data=${UEFI_DATA:-build/uefi-data.img}
if [[ ! -f "${data}" ]]; then
  "${PYTHON}" - "${data}" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
path.write_bytes(b"\x00" * (16 * 1024 * 1024))
print("created", path)
PY
fi

# Boot disk on q35 AHCI; dedicated piix3 IDE master at 0x1F0 for ATA PIO + TFS.
# USB HID interrupt IN is not wired yet; usb-kbd steals GUI keys from PS/2.
usb_hid_args=()
if [[ -n "${TESTOS_USB_HID:-}" ]]; then
  usb_hid_args+=(-device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0)
fi

exec "${QEMU64}" \
  -machine q35 -m 512M \
  -smp ${TESTOS_SMP:-4} \
  -drive if=pflash,format=raw,readonly=on,file="${code}" \
  -drive if=pflash,format=raw,file="${vars}" \
  -drive file="${image}",format=raw,if=none,id=boot \
  -device ide-hd,drive=boot,bootindex=0 \
  -device piix3-ide,id=ide \
  -drive file="${data}",format=raw,if=none,id=data \
  -device ide-hd,drive=data,bus=ide.0 \
  -netdev user,id=net0,hostfwd=tcp::8080-:8080 \
  -device e1000e,netdev=net0 \
  -device qemu-xhci,id=xhci \
  "${usb_hid_args[@]}" \
  -device bochs-display -vga none \
  -serial stdio
