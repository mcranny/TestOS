#!/usr/bin/env python3
"""Boot UEFI TestOS under QEMU and run host-side HTTP interop checks."""
from __future__ import annotations

import os
import pathlib
import socket
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
VARS = ROOT / "build" / "ovmf-vars.fd"
IMG = ROOT / "build" / "testos-usb-local.img"
DATA = ROOT / "build" / "uefi-data.img"
PORT = int(os.environ.get("TESTOS_SERIAL_PORT", "5556"))


def resolve_qemu() -> pathlib.Path:
    for key in ("TESTOS_QEMU", "QEMU"):
        value = os.environ.get(key)
        if value:
            return pathlib.Path(value)
    candidates = (
        pathlib.Path(r"C:\Program Files\qemu\qemu-system-x86_64.exe"),
        pathlib.Path("/usr/bin/qemu-system-x86_64"),
        pathlib.Path("/usr/local/bin/qemu-system-x86_64"),
    )
    for path in candidates:
        if path.is_file():
            return path
    return pathlib.Path("qemu-system-x86_64")


def resolve_ovmf_code() -> pathlib.Path:
    for key in ("TESTOS_OVMF_CODE", "OVMF_CODE"):
        value = os.environ.get(key)
        if value:
            return pathlib.Path(value)
    candidates = (
        pathlib.Path(r"C:\Program Files\qemu\share\edk2-x86_64-code.fd"),
        pathlib.Path("/usr/share/OVMF/OVMF_CODE_4M.fd"),
        pathlib.Path("/usr/share/OVMF/OVMF_CODE.fd"),
        pathlib.Path("/usr/share/edk2/ovmf/OVMF_CODE.fd"),
        pathlib.Path("/usr/share/edk2-ovmf/x64/OVMF_CODE.fd"),
    )
    for path in candidates:
        if path.is_file():
            return path
    raise FileNotFoundError(
        "OVMF code firmware not found; set TESTOS_OVMF_CODE to the firmware path"
    )


def wait_serial_prompt(proc: subprocess.Popen[bytes], timeout: float) -> None:
    deadline = time.time() + timeout
    sock = None
    buf = bytearray()
    while time.time() < deadline and sock is None:
        try:
            sock = socket.create_connection(("127.0.0.1", PORT), timeout=1)
        except OSError:
            if proc.poll() is not None:
                err = proc.stderr.read().decode("latin1", errors="replace") if proc.stderr else ""
                raise RuntimeError(f"qemu exited early: {err}") from None
            time.sleep(0.1)
    if sock is None:
        raise RuntimeError("could not connect to qemu serial")
    try:
        sock.settimeout(0.2)
        end = time.time() + timeout
        while time.time() < end:
            if b"/# " in buf:
                return
            try:
                chunk = sock.recv(4096)
                if chunk:
                    buf.extend(chunk)
            except socket.timeout:
                continue
        raise RuntimeError("shell prompt not reached before HTTP checks")
    finally:
        sock.close()


def main() -> int:
    if not IMG.is_file():
        print(f"missing boot image: {IMG}", file=sys.stderr)
        return 1

    qemu = resolve_qemu()
    code = resolve_ovmf_code()
    if not DATA.exists() or DATA.stat().st_size == 0:
        DATA.write_bytes(b"\x00" * (16 * 1024 * 1024))
    if not VARS.exists() or VARS.stat().st_size == 0:
        VARS.write_bytes(b"\x00" * code.stat().st_size)

    args = [
        str(qemu),
        "-machine", "q35",
        "-m", "512M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,file={VARS}",
        "-drive", f"file={IMG},format=raw,if=none,id=boot",
        "-device", "ide-hd,drive=boot,bootindex=0",
        "-device", "piix3-ide,id=ide",
        "-drive", f"file={DATA},format=raw,if=none,id=data",
        "-device", "ide-hd,drive=data,bus=ide.0",
        "-netdev", "user,id=net0,hostfwd=tcp::8080-:8080",
        "-device", "e1000e,netdev=net0",
        "-device", "bochs-display",
        "-vga", "none",
        "-serial", f"tcp:127.0.0.1:{PORT},server,nowait",
        "-display", "none",
        "-no-reboot",
        "-no-shutdown",
    ]
    proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        wait_serial_prompt(proc, 60.0)
        interop = ROOT / "dev" / "http-interop-test.py"
        result = subprocess.run([sys.executable, str(interop)], cwd=str(ROOT), check=False)
        return result.returncode
    finally:
        proc.kill()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            pass


if __name__ == "__main__":
    sys.exit(main())
