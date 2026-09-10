#!/usr/bin/env python3
import pathlib, socket, subprocess, sys, time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "dev"))
from ovmf_vars import ensure_ovmf_vars

QEMU = pathlib.Path(r"C:/Program Files/qemu/qemu-system-x86_64.exe")
CODE = pathlib.Path(r"C:/Program Files/qemu/share/edk2-x86_64-code.fd")
VARS = ROOT / "build" / "ovmf-vars.fd"
IMG = ROOT / "build" / "testos-usb-local.img"
DATA = ROOT / "build" / "uefi-data.img"
PORT = 5599

ensure_ovmf_vars(VARS, CODE)
if not DATA.exists():
    DATA.write_bytes(b"\x00" * (16 * 1024 * 1024))

args = [
    str(QEMU), "-machine", "q35", "-smp", "4", "-m", "512M",
    "-drive", f"if=pflash,format=raw,readonly=on,file={CODE}",
    "-drive", f"if=pflash,format=raw,file={VARS}",
    "-drive", f"file={IMG},format=raw,if=none,id=boot",
    "-device", "ide-hd,drive=boot,bootindex=0",
    "-device", "piix3-ide,id=ide",
    "-drive", f"file={DATA},format=raw,if=none,id=data",
    "-device", "ide-hd,drive=data,bus=ide.0",
    "-device", "bochs-display", "-vga", "none",
    "-serial", f"tcp:127.0.0.1:{PORT},server,nowait",
    "-display", "none", "-no-reboot", "-no-shutdown",
]
proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
buf = bytearray()
sock = None
try:
    for _ in range(100):
        try:
            sock = socket.create_connection(("127.0.0.1", PORT), timeout=1)
            break
        except OSError:
            if proc.poll() is not None:
                print(proc.stderr.read().decode("latin1", errors="replace"))
                raise SystemExit(1)
            time.sleep(0.1)
    sock.settimeout(0.2)
    end = time.time() + 45
    while time.time() < end:
        if b"BOOT: TestOS ready" in buf and b"smp cpus_online=" in buf:
            break
        if b"panic" in buf.lower() or b"CPU exception" in buf:
            break
        try:
            chunk = sock.recv(4096)
            if chunk:
                buf.extend(chunk)
        except socket.timeout:
            pass
finally:
    if sock:
        sock.close()
    proc.kill()
    proc.wait()

text = buf.decode("latin1", errors="replace")
print(text[-5000:])
ok = ("BOOT: TestOS ready" in text) and ("AP online cpu=" in text) and ("smp cpus_online=" in text)
print("RESULT", "PASS" if ok else "FAIL")
raise SystemExit(0 if ok else 1)
