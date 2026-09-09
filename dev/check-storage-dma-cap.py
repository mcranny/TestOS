#!/usr/bin/env python3
"""Static honesty check: AHCI/NVMe I/O paths refuse >8×512B DMA transfers.

Documents the single 4 KiB DMA page cap (AHCI_MAX_DMA_SECTORS / NVME_MAX_DMA_SECTORS).
Does not exercise a live >8-sector I/O; see driver early-return guards.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def require_define(path: pathlib.Path, name: str, expect: int) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(rf"#define\s+{re.escape(name)}\s+\(([^)]+)\)", text)
    if not match:
        match = re.search(rf"#define\s+{re.escape(name)}\s+(\S+)", text)
    if not match:
        raise SystemExit(f"{path}: missing #define {name}")
    expr = match.group(1)
    # Evaluate simple (PAGE / SECTOR) form used by both drivers.
    page = re.search(r"#define\s+\w*DMA_PAGE_SIZE\s+(\d+)U?", text)
    sector = re.search(r"#define\s+\w*SECTOR_SIZE\s+(\d+)U?", text)
    if "DMA_PAGE_SIZE" in expr and "SECTOR_SIZE" in expr and page and sector:
        value = int(page.group(1)) // int(sector.group(1))
    else:
        value = int(re.sub(r"U\b", "", expr), 0)
    if value != expect:
        raise SystemExit(f"{path}: {name}={value}, expected {expect}")
    print(f"OK: {path.name} {name}={value}")


def require_guard(path: pathlib.Path, needle: str) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")
    if needle not in text:
        raise SystemExit(f"{path}: missing refuse guard {needle!r}")
    print(f"OK: {path.name} refuses when {needle}")


def main() -> int:
    ahci = ROOT / "uefi" / "drivers" / "ahci.c"
    nvme = ROOT / "uefi" / "drivers" / "nvme.c"
    require_define(ahci, "AHCI_MAX_DMA_SECTORS", 8)
    require_define(nvme, "NVME_MAX_DMA_SECTORS", 8)
    require_guard(ahci, "count > AHCI_MAX_DMA_SECTORS")
    require_guard(nvme, "count > NVME_MAX_DMA_SECTORS")
    print("PASS: storage DMA capped at 8 sectors (4 KiB page); >8 refused in drivers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
