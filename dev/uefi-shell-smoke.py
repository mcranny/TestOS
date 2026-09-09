#!/usr/bin/env python3
"""Drive TestOS UEFI shell over QEMU TCP serial for acceptance checks."""
from __future__ import annotations

import os
import pathlib
import re
import socket
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
VARS = ROOT / "build" / "ovmf-vars.fd"
IMG = ROOT / "build" / "testos-usb-local.img"
DATA = ROOT / "build" / "uefi-data.img"
OUT = ROOT / "build" / "uefi-shell-test.log"
PORT = int(os.environ.get("TESTOS_SERIAL_PORT", "5555"))


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


QEMU = resolve_qemu()
CODE = resolve_ovmf_code()


def strip_ansi(text: str) -> str:
    return re.sub(r"\x1b\[[0-9;?=]*[A-Za-z]", "", text)


def recv_until(sock: socket.socket, buf: bytearray, needle: bytes, timeout: float) -> bool:
    sock.settimeout(0.2)
    end = time.time() + timeout
    while time.time() < end:
        if needle in buf:
            return True
        try:
            chunk = sock.recv(4096)
            if not chunk:
                time.sleep(0.05)
                continue
            buf.extend(chunk)
        except socket.timeout:
            continue
    return needle in buf


def drain(sock: socket.socket, buf: bytearray, settle: float) -> None:
    sock.settimeout(0.1)
    end = time.time() + settle
    while time.time() < end:
        try:
            chunk = sock.recv(4096)
            if chunk:
                buf.extend(chunk)
        except socket.timeout:
            pass


def send_cmd(sock: socket.socket, buf: bytearray, cmd: str, settle: float = 1.0) -> None:
    # QEMU 16550 FIFO is small; send slowly so the polled console does not drop bytes.
    for ch in cmd + "\r":
        sock.sendall(ch.encode("ascii"))
        time.sleep(0.02)
        drain(sock, buf, 0.01)
    drain(sock, buf, settle)


def qemu_storage_args(backend: str) -> list[str]:
    data_drive = ["-drive", f"file={DATA},format=raw,if=none,id=data"]
    if backend == "ahci":
        return [
            "-device", "ich9-ahci,id=ahci",
            "-drive", f"file={IMG},format=raw,if=none,id=boot",
            "-device", "ide-hd,drive=boot,bus=ahci.5,bootindex=0",
            *data_drive,
            "-device", "ide-hd,drive=data,bus=ahci.0",
        ]
    if backend == "nvme":
        return [
            "-drive", f"file={IMG},format=raw,if=none,id=boot",
            "-device", "ide-hd,drive=boot,bootindex=0",
            *data_drive,
            "-device", "nvme,serial=testos,drive=data",
        ]
    return [
        "-drive", f"file={IMG},format=raw,if=none,id=boot",
        "-device", "ide-hd,drive=boot,bootindex=0",
        "-device", "piix3-ide,id=ide",
        *data_drive,
        "-device", "ide-hd,drive=data,bus=ide.0",
    ]


def run_boot(fresh_data: bool, commands: list[tuple[str, float]], backend: str = "ata") -> str:
    if fresh_data:
        DATA.write_bytes(b"\x00" * (16 * 1024 * 1024))
    sys.path.insert(0, str(ROOT / "dev"))
    from ovmf_vars import ensure_ovmf_vars

    ensure_ovmf_vars(VARS, CODE)

    args = [
        str(QEMU),
        "-machine", "q35",
        "-m", "512M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={CODE}",
        "-drive", f"if=pflash,format=raw,file={VARS}",
        *qemu_storage_args(backend),
        "-device", "qemu-xhci,id=xhci",
        "-device", "usb-kbd,bus=xhci.0",
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
    buf = bytearray()
    sock = None
    try:
        connect_deadline = time.time() + 10
        while time.time() < connect_deadline:
            try:
                sock = socket.create_connection(("127.0.0.1", PORT), timeout=1)
                break
            except OSError:
                if proc.poll() is not None:
                    err = proc.stderr.read().decode("latin1", errors="replace")
                    raise RuntimeError(f"qemu exited early: {err}") from None
                time.sleep(0.1)
        if sock is None:
            raise RuntimeError("could not connect to qemu serial")

        if not recv_until(sock, buf, b"/# ", 45.0):
            raise RuntimeError("shell prompt not reached")
        for cmd, settle in commands:
            send_cmd(sock, buf, cmd, settle)
        drain(sock, buf, 0.5)
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass
        proc.kill()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            pass
    text = buf.decode("latin1", errors="replace")
    OUT.write_text(text, encoding="utf-8")
    return text


def check(text: str, needle: str, name: str) -> bool:
    norm = text.replace("\r\n", "\n").replace("\r", "\n")
    want = needle.replace("\r\n", "\n").replace("\r", "\n")
    hit = want in norm
    print(f"  [{'OK' if hit else 'FAIL'}] {name}: {needle!r}")
    return hit


def main() -> int:
    backend = "ata"
    if len(sys.argv) > 1:
        backend = sys.argv[1]
    if backend not in ("ata", "ahci", "nvme"):
        print(f"unknown backend: {backend} (expected ata|ahci|nvme)", file=sys.stderr)
        return 2

    storage_needles = {
        "ata": "ATA: hd0 TFS region ready",
        "ahci": "AHCI: sata0",
        "nvme": "NVME: nvme0 ready",
    }
    cmds = [
        ("ls", 1.2),
        ("write persist.txt hello-tfs", 1.5),
        ("cat persist.txt", 1.2),
        ("fsck", 2.0),
        ("calc 2+3*4", 4.0),
        ("./calc 10-3", 4.0),
        ("ps", 1.5),
        ("ls", 1.2),
    ]
    print(f"boot1 (fresh data, backend={backend})...")
    text1 = run_boot(True, cmds, backend)
    ok = True
    ok &= check(text1, "preempt OK", "preempt")
    ok &= check(text1, storage_needles[backend], f"storage ({backend})")
    ok &= check(text1, "XHCI: Controller running", "xhci")
    ok &= check(text1, "tfs mount OK", "tfs")
    ok &= check(text1, "seeded /calc", "seed")
    ok &= check(text1, "ring3 hello OK", "hello")
    ok &= check(text1, "\nhello-tfs\n", "persist write/cat")
    ok &= check(text1, "fsck: ok", "tfs fsck after write")
    ok &= check(text1, "\n14\n", "calc 2+3*4")
    ok &= check(text1, "\n7\n", "calc 10-3")
    ok &= check(text1, "PID STATE NAME", "ps")

    print("boot2 (persist)...")
    text2 = run_boot(
        False,
        [("cat persist.txt", 1.2), ("fsck", 2.0), ("ls", 1.2)],
        backend,
    )
    ok &= check(text2, "tfs mount OK", "tfs remount")
    formatted = "tfs mount OK (formatted)" in text2
    print(f"  [{'OK' if not formatted else 'FAIL'}] no reformat on remount")
    ok &= not formatted
    ok &= check(text2, "hello-tfs", "persist across reboot")
    ok &= check(text2, "fsck: ok", "tfs fsck after remount")
    ok &= check(text2, "calc", "calc still listed")

    print("--- boot1 tail ---")
    print("\n".join(strip_ansi(text1).splitlines()[-50:]))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
