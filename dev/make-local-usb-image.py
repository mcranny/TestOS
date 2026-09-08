#!/usr/bin/env python3
"""Build a GPT disk with a FAT32 ESP for local QEMU UEFI boots (Windows/MSYS)."""
from __future__ import annotations

import pathlib
import shutil
import struct
import uuid
import zlib

from pyfatfs.PyFat import PyFat
from pyfatfs.PyFatFS import PyFatFS

SECTOR = 512
DISK_SIZE = 70 * 1024 * 1024
ESP_LBA = 2048
ESP_SIZE = 63 * 1024 * 1024


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def gpt_header(
    disk_guid: uuid.UUID,
    part_lba: int,
    part_count: int,
    part_array_crc: int,
    alt_lba: int,
    first_usable: int,
    last_usable: int,
    this_lba: int,
) -> bytes:
    hdr = bytearray(SECTOR)
    hdr[0:8] = b"EFI PART"
    struct.pack_into(
        "<IIIIQQQQ",
        hdr,
        8,
        0x00010000,
        92,
        0,
        0,
        this_lba,
        alt_lba,
        first_usable,
        last_usable,
    )
    hdr[56:72] = disk_guid.bytes_le
    struct.pack_into("<QII", hdr, 72, part_lba, part_count, 128)
    struct.pack_into("<I", hdr, 88, part_array_crc)
    struct.pack_into("<I", hdr, 16, crc32(hdr[:92]))
    return bytes(hdr)


def write_esp_contents(fat_path: pathlib.Path, stage: pathlib.Path) -> None:
    with open(fat_path, "wb") as f:
        f.truncate(ESP_SIZE)
    fat = PyFat()
    fat.mkfs(str(fat_path), fat_type=PyFat.FAT_TYPE_FAT32, size=ESP_SIZE, label="TESTOS")
    fat.close()

    fs = PyFatFS(str(fat_path), preserve_case=True, read_only=False)
    try:
        fs.makedir("/EFI")
        fs.makedir("/EFI/BOOT")
        fs.makedir("/boot")
        for dest, src in (
            ("/EFI/BOOT/BOOTX64.EFI", stage / "EFI" / "BOOT" / "BOOTX64.EFI"),
            ("/EFI/BOOT/limine.conf", stage / "EFI" / "BOOT" / "limine.conf"),
            ("/boot/testos-uefi.elf", stage / "boot" / "testos-uefi.elf"),
        ):
            with fs.open(dest, "wb") as out, open(src, "rb") as inp:
                out.write(inp.read())
    finally:
        fs.close()


def main() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    build = root / "build"
    stage = build / "esp"
    elf = build / "testos-uefi.elf"
    limine_efi = build / "limine-bin" / "BOOTX64.EFI"
    conf = root / "limine.conf"
    image = build / "testos-usb-local.img"
    fat_path = build / "esp-fat.img"

    if not elf.is_file():
        raise SystemExit(f"missing {elf}")
    if not limine_efi.is_file():
        raise SystemExit(f"missing {limine_efi}")
    if not conf.is_file():
        raise SystemExit(f"missing {conf}")

    (stage / "EFI" / "BOOT").mkdir(parents=True, exist_ok=True)
    (stage / "boot").mkdir(parents=True, exist_ok=True)
    shutil.copy2(limine_efi, stage / "EFI" / "BOOT" / "BOOTX64.EFI")
    conf_bytes = conf.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    (stage / "EFI" / "BOOT" / "limine.conf").write_bytes(conf_bytes)
    (stage / "boot" / "limine.conf").write_bytes(conf_bytes)
    (stage / "limine.conf").write_bytes(conf_bytes)
    shutil.copy2(elf, stage / "boot" / "testos-uefi.elf")

    write_esp_contents(fat_path, stage)

    disk = bytearray(DISK_SIZE)
    esp_end_lba = ESP_LBA + (ESP_SIZE // SECTOR) - 1
    last_lba = (DISK_SIZE // SECTOR) - 1
    part_guid = uuid.uuid4()
    disk_guid = uuid.uuid4()

    disk[446] = 0x00
    disk[450] = 0xEE
    struct.pack_into("<I", disk, 454, 1)
    struct.pack_into("<I", disk, 458, last_lba)
    disk[510:512] = b"\x55\xaa"

    part_entry = bytearray(128)
    part_entry[0:16] = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B").bytes_le
    part_entry[16:32] = part_guid.bytes_le
    struct.pack_into("<QQ", part_entry, 32, ESP_LBA, esp_end_lba)
    name = "TestOS EFI".encode("utf-16le")
    part_entry[56 : 56 + len(name)] = name

    part_array = bytes(part_entry) + bytes(128 * 127)
    part_crc = crc32(part_array)

    primary = gpt_header(
        disk_guid,
        part_lba=2,
        part_count=128,
        part_array_crc=part_crc,
        alt_lba=last_lba,
        first_usable=ESP_LBA,
        last_usable=esp_end_lba,
        this_lba=1,
    )
    backup = gpt_header(
        disk_guid,
        part_lba=last_lba - 32,
        part_count=128,
        part_array_crc=part_crc,
        alt_lba=1,
        first_usable=ESP_LBA,
        last_usable=esp_end_lba,
        this_lba=last_lba,
    )

    disk[SECTOR : SECTOR * 2] = primary
    disk[SECTOR * 2 : SECTOR * 2 + len(part_array)] = part_array
    fat_bytes = fat_path.read_bytes()
    disk[ESP_LBA * SECTOR : ESP_LBA * SECTOR + len(fat_bytes)] = fat_bytes
    disk[(last_lba - 32) * SECTOR : (last_lba - 32) * SECTOR + len(part_array)] = part_array
    disk[last_lba * SECTOR : (last_lba + 1) * SECTOR] = backup

    image.write_bytes(disk)
    print(f"wrote {image} ({DISK_SIZE} bytes)")


if __name__ == "__main__":
    main()
