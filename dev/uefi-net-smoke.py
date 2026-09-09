#!/usr/bin/env python3
"""UEFI networking smoke: ifconfig, DHCP path, ping, wget size, TCP echo+PCAP, HTTP.

What this proves under QEMU/slirp (not claimed elsewhere in CI):
  - Structured ifconfig status lines (not bare argv echo of 10.0.2.15)
  - Boot DHCP ACK vs static-fallback WARN (paths distinguished)
  - ICMP echo reply from gateway
  - Guest wget of guest HTTP with exact body length (HTTP_BODY_LEN=1400)
  - Host→guest TCP echo (25×1021B) via QEMU hostfwd
  - PCAP handshake/data/FIN for that echo (tcp-pcap-check.py)

Not covered: TESTOS_TCP_SELFTEST / active-open retransmit hooks (compile-time i386-era).
"""
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
PCAP = ROOT / "build" / "uefi-net-tcp.pcap"
PORT = int(os.environ.get("TESTOS_SERIAL_PORT", "5557"))

# Must match uefi/net/http.c HTTP_BODY_LEN (guest self-served GET / body).
HTTP_BODY_LEN = 1400


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


def normalize(text: str) -> str:
    return strip_ansi(text).replace("\r\n", "\n").replace("\r", "\n")


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


def check(ok: bool, name: str) -> bool:
    print(f"  [{'OK' if ok else 'FAIL'}] {name}")
    return ok


def check_re(text: str, pattern: str, name: str) -> bool:
    hit = re.search(pattern, text, re.MULTILINE) is not None
    return check(hit, name)


def main() -> int:
    if not IMG.is_file():
        print(f"missing boot image: {IMG}", file=sys.stderr)
        return 1

    qemu = resolve_qemu()
    code = resolve_ovmf_code()
    DATA.write_bytes(b"\x00" * (16 * 1024 * 1024))
    sys.path.insert(0, str(ROOT / "dev"))
    from ovmf_vars import ensure_ovmf_vars

    ensure_ovmf_vars(VARS, code)
    if PCAP.exists():
        PCAP.unlink()

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
        "-object", f"filter-dump,id=tcpdump,netdev=net0,file={PCAP}",
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

        # ifconfig labels cannot be satisfied by typing argv that contains 10.0.2.15.
        send_cmd(sock, buf, "ifconfig", 2.0)
        send_cmd(sock, buf, "ping 10.0.2.2 2", 8.0)
        send_cmd(sock, buf, "dns 10.0.2.3", 3.0)
        send_cmd(sock, buf, "wget 10.0.2.15:8080/ /http-dl.txt", 12.0)
        send_cmd(sock, buf, "cat /http-dl.txt", 2.0)
        send_cmd(sock, buf, "arp", 1.5)
        send_cmd(sock, buf, "netstat", 2.0)

        text = normalize(buf.decode("latin1", errors="replace"))
        OUT.write_text(text, encoding="utf-8")

        print("guest serial checks...")
        dhcp_ack = "DHCP: ACK received" in text
        dhcp_fail = "DHCP failed; using static defaults" in text
        ok &= check(dhcp_ack and not dhcp_fail, "DHCP ACK (not static fallback)")
        ok &= check_re(
            text,
            r"^Interface:\s+e1000e0\s*$",
            "ifconfig Interface: e1000e0",
        )
        ok &= check_re(
            text,
            r"^MAC:\s+[0-9a-f]{2}(?::[0-9a-f]{2}){5}\s*$",
            "ifconfig MAC line",
        )
        ok &= check_re(
            text,
            r"^IP:\s+10\.0\.2\.15\s*$",
            "ifconfig IP: 10.0.2.15",
        )
        ok &= check_re(
            text,
            r"^Mask:\s+255\.255\.255\.0\s*$",
            "ifconfig Mask line",
        )
        ok &= check_re(
            text,
            r"^Gateway:\s+10\.0\.2\.2\s*$",
            "ifconfig Gateway: 10.0.2.2",
        )
        ok &= check_re(
            text,
            r"^DNS:\s+10\.0\.2\.3\s*$",
            "ifconfig DNS: 10.0.2.3",
        )
        ok &= check_re(
            text,
            r"^RX:\s+[1-9][0-9]*\s+packets\s*$",
            "ifconfig RX packets > 0",
        )
        ok &= check("Reply from 10.0.2.2" in text, "ping gateway reply")
        ok &= check(
            f"wget: saved {HTTP_BODY_LEN} bytes" in text,
            f"wget saved exactly {HTTP_BODY_LEN} bytes",
        )
        ok &= check("TestOS HTTP Server" in text, "wget body in TFS")

        print("host TCP echo interop...")
        tcp = subprocess.run(
            [sys.executable, str(ROOT / "dev" / "tcp-interop-test.py")],
            cwd=str(ROOT),
            check=False,
        )
        ok &= check(tcp.returncode == 0, "tcp-interop (25×1021B echo)")

        print("TCP PCAP checks...")
        if not PCAP.is_file() or PCAP.stat().st_size == 0:
            ok &= check(False, f"pcap capture present ({PCAP.name})")
        else:
            pcap = subprocess.run(
                [sys.executable, str(ROOT / "dev" / "tcp-pcap-check.py"), str(PCAP)],
                cwd=str(ROOT),
                check=False,
            )
            ok &= check(pcap.returncode == 0, "tcp-pcap handshake/data/FIN")

        print("host HTTP interop...")
        http = subprocess.run(
            [sys.executable, str(ROOT / "dev" / "http-interop-test.py")],
            cwd=str(ROOT),
            check=False,
        )
        ok &= check(http.returncode == 0, "http-interop")

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
