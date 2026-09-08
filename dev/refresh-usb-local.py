#!/usr/bin/env python3
"""Refresh /boot/testos-uefi.elf inside build/testos-usb-local.img (GPT+FAT ESP)."""
from __future__ import annotations

import pathlib
import shutil
import sys

ESP_OFFSET = 2048 * 512


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    elf = root / "build" / "testos-uefi.elf"
    image = root / "build" / "testos-usb-local.img"
    esp_elf = root / "build" / "esp" / "boot" / "testos-uefi.elf"

    if not elf.is_file():
        print(f"missing kernel: {elf}", file=sys.stderr)
        return 1
    if not image.is_file():
        print(f"missing image: {image}", file=sys.stderr)
        print("recreate with: python dev/make-local-usb-image.py", file=sys.stderr)
        return 1

    esp_elf.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(elf, esp_elf)

    from pyfatfs.PyFatFS import PyFatFS

    data = elf.read_bytes()
    fs = PyFatFS(str(image), offset=ESP_OFFSET, preserve_case=True, read_only=False)
    try:
        try:
            fs.remove("/boot/testos-uefi.elf")
        except Exception:
            pass
        with fs.open("/boot/testos-uefi.elf", "wb") as f:
            f.write(data)
        size = fs.getsize("/boot/testos-uefi.elf")
    finally:
        fs.close()

    print(f"updated {image} (/boot/testos-uefi.elf, {size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
