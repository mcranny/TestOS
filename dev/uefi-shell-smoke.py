#!/usr/bin/env python3
"""Drive TestOS UEFI shell over QEMU TCP serial for acceptance checks."""
from __future__ import annotations

import pathlib
import re
import socket
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
QEMU = pathlib.Path(r"C:\Program Files\qemu\qemu-system-x86_64.exe")
CODE = pathlib.Path(r"C:\Program Files\qemu\share\edk2-x86_64-code.fd")
VARS = ROOT / "build" / "ovmf-vars.fd"
IMG = ROOT / "build" / "testos-usb-local.img"
DATA = ROOT / "build" / "uefi-data.img"
OUT = ROOT / "build" / "uefi-shell-test.log"
PORT = 5555


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


def run_boot(fresh_data: bool, commands: list[tuple[str, float]]) -> str:
    if fresh_data:
        DATA.write_bytes(b"\x00" * (16 * 1024 * 1024))
    if not VARS.exists() or VARS.stat().st_size == 0:
        VARS.write_bytes(b"\x00" * CODE.stat().st_size)

    args = [
        str(QEMU),
        "-machine", "q35",
        "-m", "512M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={CODE}",
        "-drive", f"if=pflash,format=raw,file={VARS}",
        "-drive", f"file={IMG},format=raw,if=none,id=boot",
        "-device", "ide-hd,drive=boot,bootindex=0",
        "-device", "piix3-ide,id=ide",
        "-drive", f"file={DATA},format=raw,if=none,id=data",
        "-device", "ide-hd,drive=data,bus=ide.0",
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
        # Connect once QEMU is listening
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
    cmds = [
        ("ls", 1.2),
        ("write persist.txt hello-tfs", 1.5),
        ("cat persist.txt", 1.2),
        ("calc 2+3*4", 4.0),
        ("./calc 10-3", 4.0),
        ("ps", 1.5),
        ("ls", 1.2),
    ]
    print("boot1 (fresh data)...")
    text1 = run_boot(True, cmds)
    ok = True
    ok &= check(text1, "preempt OK", "preempt")
    ok &= check(text1, "ATA: hd0 TFS region ready", "ata")
    ok &= check(text1, "tfs mount OK", "tfs")
    ok &= check(text1, "seeded /calc", "seed")
    ok &= check(text1, "ring3 hello OK", "hello")
    ok &= check(text1, "hello-tfs", "persist write/cat")
    ok &= check(text1, "\n14\n", "calc 2+3*4")
    ok &= check(text1, "\n7\n", "calc 10-3")
    ok &= check(text1, "PID STATE NAME", "ps")

    print("boot2 (persist)...")
    text2 = run_boot(False, [("cat persist.txt", 1.2), ("ls", 1.2)])
    ok &= check(text2, "tfs mount OK", "tfs remount")
    formatted = "tfs mount OK (formatted)" in text2
    print(f"  [{'OK' if not formatted else 'FAIL'}] no reformat on remount")
    ok &= not formatted
    ok &= check(text2, "hello-tfs", "persist across reboot")
    ok &= check(text2, "calc", "calc still listed")

    print("--- boot1 tail ---")
    print("\n".join(strip_ansi(text1).splitlines()[-50:]))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
