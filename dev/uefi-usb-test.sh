#!/usr/bin/env bash
# Validate the exact raw image as a UEFI disk; it deliberately never uses -kernel.
set -euo pipefail

image=${1:-build/testos-usb.img}
report=build/uefi-validation.txt
test -f "${image}" || { echo "image not found: ${image}" >&2; exit 1; }
command -v sgdisk >/dev/null
command -v mdir >/dev/null
command -v qemu-system-x86_64 >/dev/null
code=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
vars_template=${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}
test -r "${code}" && test -r "${vars_template}" || { echo "OVMF firmware files are unavailable" >&2; exit 1; }

sgdisk --verify "${image}"
sgdisk --print "${image}" | grep -Eq 'EF00.*TestOS EFI'
offset=$((2048 * 512))
mdir -i "${image}@@${offset}" ::/EFI/BOOT/BOOTX64.EFI >/dev/null
mdir -i "${image}@@${offset}" ::/boot/testos-uefi.elf >/dev/null

boot_once() {
    local ram=$1 log=$2 vars monitor screenshot qemu_pid status=0
    vars=$(mktemp)
    monitor=$(mktemp -u)
    screenshot="${log%.log}.ppm"
    cp "${vars_template}" "${vars}"
    qemu-system-x86_64 \
        -machine q35 -m "${ram}" -nodefaults -vga none -device bochs-display \
        -drive if=pflash,format=raw,readonly=on,file="${code}" \
        -drive if=pflash,format=raw,file="${vars}" \
        -drive file="${image}",format=raw,if=ide \
        -display none -serial "file:${log}" -monitor "unix:${monitor},server,nowait" \
        -no-reboot -no-shutdown &
    qemu_pid=$!
    for _ in $(seq 1 100); do
        grep -q 'BOOT: TestOS ready' "${log}" 2>/dev/null && break
        sleep 0.1
    done
    if [ -S "${monitor}" ]; then printf 'screendump %s\nquit\n' "${screenshot}" | nc -U "${monitor}" >/dev/null || true; fi
    kill "${qemu_pid}" 2>/dev/null || true
    wait "${qemu_pid}" || status=$?
    rm -f "${vars}"
    if ! grep -q 'BOOT: TestOS ready' "${log}"; then
        cat "${log}" >&2
        return 1
    fi
    grep -Eq 'framebuffer physical=0x[0-9a-f]{16}' "${log}"
    python3 - "${screenshot}" <<'PY'
import sys
p = open(sys.argv[1], 'rb').read()
assert p.startswith(b'P6\n'), 'framebuffer screenshot was not captured'
assert len(set(p[64:])) > 3, 'framebuffer screenshot lacks rendered content'
PY
    test "${status}" -eq 124 || true
}

mkdir -p build
rm -f build/uefi-serial-*.log
{
    echo "TestOS UEFI framebuffer validation"
    echo "source_revision=$(git rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "image=$(basename "${image}")"
    echo "image_sha256=$(sha256sum "${image}" | awk '{print $1}')"
    echo "firmware_code=${code}"
    echo "firmware_vars_template=${vars_template}"
    echo "qemu_machine=q35"
    echo "qemu_display=bochs-display,-vga none"
    echo "qemu_boot=exact raw GPT image; no -kernel"
} > "${report}"
for iteration in 1 2 3 4 5; do
    boot_once 256M "build/uefi-serial-${iteration}.log"
    echo "boot=256M-${iteration} result=TestOS ready framebuffer=$(sed -n 's/.*framebuffer physical=//p' "build/uefi-serial-${iteration}.log" | tail -1)" >> "${report}"
done
boot_once 512M build/uefi-serial-512m.log
echo "boot=512M result=TestOS ready framebuffer=$(sed -n 's/.*framebuffer physical=//p' build/uefi-serial-512m.log | tail -1)" >> "${report}"
echo "UEFI USB validation passed without legacy VGA: five cold boots at 256 MiB and one at 512 MiB."
echo "Evidence report: ${report}"
