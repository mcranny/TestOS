#!/usr/bin/env python3
"""Create or copy an OVMF VARS pflash image for QEMU.

Zero-filling VARS to the CODE firmware size is wrong for classic OVMF
(CODE ~2 MiB, VARS 128–512 KiB). Prefer a real VARS template; otherwise
use a fixed 256 KiB blank image. For the 4 MiB OVMF platform, prefer
OVMF_VARS_4M.fd (same size as CODE) when present.
"""
from __future__ import annotations

import pathlib
import shutil
import sys

OVMF_VARS_CLASSIC = 256 * 1024


def companion_vars_candidates(code: pathlib.Path) -> list[pathlib.Path]:
    name = code.name
    parent = code.parent
    out: list[pathlib.Path] = []
    # Same directory renames (edk2 / distro layouts).
    replacements = (
        ("CODE", "VARS"),
        ("code", "vars"),
        ("_CODE", "_VARS"),
        ("-code", "-vars"),
    )
    for a, b in replacements:
        if a in name:
            out.append(parent / name.replace(a, b, 1))
    # Distro defaults.
    out.extend(
        [
            pathlib.Path("/usr/share/OVMF/OVMF_VARS_4M.fd"),
            pathlib.Path("/usr/share/OVMF/OVMF_VARS.fd"),
            pathlib.Path("/usr/share/edk2/ovmf/OVMF_VARS.fd"),
            pathlib.Path("/usr/share/edk2-ovmf/x64/OVMF_VARS.fd"),
            pathlib.Path("build/ovmf/RELEASEX64_OVMF_VARS.fd"),
            pathlib.Path(r"C:\Program Files\qemu\share\edk2-x86_64-vars.fd"),
        ]
    )
    return out


def blank_vars_size(code: pathlib.Path) -> int:
    """Size for a synthetic blank VARS when no template exists."""
    # 4 MiB CODE platforms require a matching 4 MiB VARS pflash in QEMU.
    if "4M" in code.name.upper() or code.stat().st_size >= 4 * 1024 * 1024:
        return code.stat().st_size
    return OVMF_VARS_CLASSIC


def ensure_ovmf_vars(vars_path: pathlib.Path, code_path: pathlib.Path) -> None:
    if vars_path.exists() and vars_path.stat().st_size > 0:
        return
    vars_path.parent.mkdir(parents=True, exist_ok=True)
    for cand in companion_vars_candidates(code_path):
        if cand.is_file() and cand.stat().st_size > 0:
            shutil.copyfile(cand, vars_path)
            return
    size = blank_vars_size(code_path)
    vars_path.write_bytes(b"\x00" * size)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(f"usage: {argv[0]} <ovmf-code.fd> <ovmf-vars.fd>", file=sys.stderr)
        return 2
    code = pathlib.Path(argv[1])
    vars_path = pathlib.Path(argv[2])
    if not code.is_file():
        print(f"missing OVMF CODE: {code}", file=sys.stderr)
        return 1
    ensure_ovmf_vars(vars_path, code)
    print(f"ovmf vars ready: {vars_path} ({vars_path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
