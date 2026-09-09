#!/usr/bin/env python3
"""UEFI networking smoke: DHCP/config, ping, DNS, TCP echo, HTTP, wget→TFS."""
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
DATA = ROOT / "build" / "uefi-net-data.img"
OUT = ROOT / "build" / "uefi-net-smoke.log"
PORT = int(os.environ.get("TESTOS_SERIAL_PORT", "5557"))


def resolve_qemu() -> pathlib.Path:
    for key in ("TESTOS_QEMU", "QEMU"):
        value = os.environ.get(key)
        if value:
            return pathlib.Path(value)
    for path in (
        pathlib.Path(r"C:\Program Files\qemu\qemu-system-x86_64.exe"),
        pathlib.Path("/usr/bin/qemu-system-x86_64"),
        pathlib.Path("/usr/local/bin/qemu-system-x86_64"),
    ):
        if path.is_file():
            return path
    return pathlib.Path("qemu-system-x86_64")


def resolve_ovmf_code() -> pathlib.Path:
    for key in ("TESTOS_OVMF_CODE", "OVMF_CODE"):
        value = os.environ.get(key)
        if value:
            return pathlib.Path(value)
    for path in (
        pathlib.Path(r"C:\Program Files\qemu\share\edk2-x86_64-code.fd"),
        pathlib.Path("/usr/share/OVMF/OVMF_CODE_4M.fd"),
        pathlib.Path("/usr/share/OVMF/OVMF_CODE.fd"),
        pathlib.Path("/usr/share/edk2/ovmf/OVMF_CODE.fd"),
        pathlib.Path("/usr/share/edk2-ovmf/x64/OVMF_CODE.fd"),
    ):
        if path.is_file():
            return path
    raise FileNotFoundError("OVMF code firmware not found; set TESTOS_OVMF_CODE")


def strip_ansi(text: str) -> str:
    return re.sub(r"\x1b\[[0-9;?=]*[A-Za-z]", "", text)


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


def recv_until(sock: socket.socket, buf: bytearray, needle: bytes, timeout: float) -> bool:
    sock.settimeout(0.2)
    end = time.time() + timeout
    while time.time() < end:
        if needle in buf:
            return True
        try:
            chunk = sock.recv(4096)
            if chunk:
                buf.extend(chunk)
        except socket.timeout:
            continue
    return needle in buf


def send_cmd(sock: socket.socket, buf: bytearray, cmd: str, settle: float = 1.2) -> None:
    for ch in cmd + "\r":
        sock.sendall(ch.encode("ascii"))
        time.sleep(0.02)
        drain(sock, buf, 0.01)
    drain(sock, buf, settle)


def check(text: str, needle: str, name: str) -> bool:
    ok = needle in text
    print(f"  [{'OK' if ok else 'FAIL'}] {name}")
    return ok


def main() -> int:
    if not IMG.is_file():
        print(f"missing boot image: {IMG}", file=sys.stderr)
        return 1

    qemu = resolve_qemu()
    code = resolve_ovmf_code()
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
        "-netdev", "user,id=net0,hostfwd=tcp::8080-:8080,hostfwd=tcp::12346-:12346",
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
    ok = True
    try:
        deadline = time.time() + 15
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

        if not recv_until(sock, buf, b"/# ", 90.0):
            raise RuntimeError("shell prompt not reached")

        send_cmd(sock, buf, "netstat", 2.0)
        send_cmd(sock, buf, "ping 10.0.2.2 2", 8.0)
        send_cmd(sock, buf, "dns 10.0.2.2", 3.0)
        send_cmd(sock, buf, "wget 10.0.2.15:8080/ /http-dl.txt", 12.0)
        send_cmd(sock, buf, "cat /http-dl.txt", 2.0)
        send_cmd(sock, buf, "arp", 1.5)

        text = strip_ansi(buf.decode("latin1", errors="replace"))
        OUT.write_text(text, encoding="utf-8")

        print("guest serial checks...")
        ok &= check(text, "10.0.2.15", "guest IP configured")
        ok &= check(text, "10.0.2.2", "gateway present")
        ok &= check(text, "Reply from 10.0.2.2", "ping gateway")
        ok &= check(text, "Interface: e1000e0", "netstat iface")
        ok &= check(text, "wget: saved", "wget download")
        ok &= check(text, "TestOS HTTP Server", "wget body in TFS")

        print("host TCP echo interop...")
        tcp = subprocess.run(
            [sys.executable, str(ROOT / "dev" / "tcp-interop-test.py")],
            cwd=str(ROOT),
            check=False,
        )
        print(f"  [{'OK' if tcp.returncode == 0 else 'FAIL'}] tcp-interop")
        ok &= tcp.returncode == 0

        print("host HTTP interop...")
        http = subprocess.run(
            [sys.executable, str(ROOT / "dev" / "http-interop-test.py")],
            cwd=str(ROOT),
            check=False,
        )
        print(f"  [{'OK' if http.returncode == 0 else 'FAIL'}] http-interop")
        ok &= http.returncode == 0

        return 0 if ok else 1
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


if __name__ == "__main__":
    sys.exit(main())
