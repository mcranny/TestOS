#!/usr/bin/env bash
# Build a raw, whole-device UEFI image. It only writes files below build/.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "${repo_root}"

image=build/testos-usb.img
esp=build/testos-esp.fat
stage=build/usb-stage
esp_size_mb=63
image_size_mb=65

for program in sgdisk mformat mmd mcopy dd sha256sum truncate; do
    command -v "${program}" >/dev/null || {
        echo "missing required image tool: ${program}" >&2
        exit 1
    }
done

test -s build/kernel.bin || { echo "missing build/kernel.bin; run make first" >&2; exit 1; }
test -r /opt/limine/BOOTX64.EFI || { echo "missing pinned Limine UEFI loader" >&2; exit 1; }
rm -f "${image}" "${esp}"
rm -rf "${stage}"
mkdir -p build
mkdir -p "${stage}"

# FAT otherwise records host timestamps and a generated serial number. Stage
# the files with a fixed valid FAT timestamp and use a fixed volume serial.
cp /opt/limine/BOOTX64.EFI "${stage}/BOOTX64.EFI"
cp limine.conf "${stage}/limine.conf"
cp build/testos-uefi.elf "${stage}/testos-uefi.elf"
touch -d '@315532800' "${stage}/BOOTX64.EFI" "${stage}/limine.conf" "${stage}/testos-uefi.elf"

truncate -s "${image_size_mb}M" "${image}"
sgdisk --zap-all "${image}" >/dev/null
sgdisk --clear --disk-guid=7A0E5E0E-5E0E-4A11-8E11-544553544F53 \
    --new=1:2048:131071 --partition-guid=1:0E5E0E5E-5445-4F53-8E11-544553544F53 \
    --typecode=1:ef00 --change-name=1:'TestOS EFI' "${image}" >/dev/null
truncate -s "${esp_size_mb}M" "${esp}"
mformat -i "${esp}" -F -N 54455354 -v TESTOS ::
mmd -i "${esp}" ::/EFI ::/EFI/BOOT ::/boot
mcopy -m -i "${esp}" "${stage}/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -m -i "${esp}" "${stage}/limine.conf" ::/EFI/BOOT/limine.conf
mcopy -m -i "${esp}" "${stage}/testos-uefi.elf" ::/boot/testos-uefi.elf
python3 dev/normalize-fat-timestamps.py "${esp}"

# The ESP is exactly the GPT partition size and begins at the fixed 1 MiB offset.
dd if="${esp}" of="${image}" bs=1M seek=1 conv=notrunc status=none
rm -rf "${esp}" "${stage}"
echo "UEFI USB image: ${image}"
echo "Bytes: $(wc -c < "${image}")"
sha256sum "${image}"
