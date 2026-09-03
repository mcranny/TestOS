#!/usr/bin/env python3
"""Host-side TCP regression client for TestOS' QEMU user-network port forward."""

import socket
import sys
import time

HOST = "127.0.0.1"
PORT = 12346
ATTEMPTS = 80


def exchange(payload: bytes) -> None:
    last_error = None
    for _ in range(ATTEMPTS):
        try:
            with socket.create_connection((HOST, PORT), timeout=1) as sock:
                sock.settimeout(5)
                sock.sendall(payload)
                received = bytearray()
                while len(received) < len(payload):
                    chunk = sock.recv(len(payload) - len(received))
                    if not chunk:
                        raise AssertionError("guest closed before echoing the payload")
                    received.extend(chunk)
                if bytes(received) != payload:
                    raise AssertionError("echo payload differs from request")
                return
        except (ConnectionError, OSError, AssertionError) as error:
            last_error = error
            time.sleep(0.1)
    raise RuntimeError(f"TCP exchange failed: {last_error}")


def main() -> int:
    for index in range(25):
        payload = (f"testos-tcp-{index:02d}-".encode() + b"x" * 1007)
        exchange(payload)
    print("PASS: 25 TCP echo connections, 1021 bytes each")
    return 0


if __name__ == "__main__":
    sys.exit(main())
