#!/usr/bin/env python3
"""Bounded HTTP interoperability checks for the guest socket service."""
import concurrent.futures
import http.client
import socket
import time

HOST, PORT = "127.0.0.1", 8080
def request(method="GET", path="/", fragmented=False):
    if fragmented:
        with socket.create_connection((HOST, PORT), timeout=2) as sock:
            sock.settimeout(5); sock.sendall(b"GET / HTTP/1.0\r\nHost: test"); time.sleep(.03); sock.sendall(b"\r\n\r\n")
            raw = bytearray()
            while True:
                part = sock.recv(4096)
                if not part: break
                raw.extend(part)
        head, body = bytes(raw).split(b"\r\n\r\n", 1)
        assert b"200 OK" in head and int([x for x in head.split(b"\r\n") if x.startswith(b"Content-Length:")][0].split(b":")[1]) == len(body)
        return body
    c = http.client.HTTPConnection(HOST, PORT, timeout=5); c.request(method, path); r = c.getresponse(); body = r.read(); c.close(); return r.status, dict(r.getheaders()), body
def raw_status(payload):
    with socket.create_connection((HOST, PORT), timeout=2) as sock:
        sock.settimeout(5); sock.sendall(payload)
        raw = bytearray()
        while True:
            part = sock.recv(4096)
            if not part: break
            raw.extend(part)
    return int(bytes(raw).split(b" ", 2)[1])
def main():
    last = None
    for _ in range(80):
        try:
            body = request(fragmented=True)
            if body: break
        except (OSError, http.client.HTTPException) as exc: last = exc
        time.sleep(.1)
    else: raise RuntimeError(f"HTTP service did not become ready: {last}")
    assert len(body) > 1024 and b"TestOS HTTP Server" in body
    status, headers, body_again = request()
    assert status == 200 and headers["Content-Length"] == str(len(body_again)) and body_again == body
    assert request("GET", "/missing")[0] == 404 and request("POST", "/")[0] == 405
    assert raw_status(b"not an HTTP request\r\n\r\n") == 400
    assert raw_status(b"GET / HTTP/1.0\r\nX: " + b"x" * 1100) == 400
    with socket.create_connection((HOST, PORT), timeout=2) as sock: sock.sendall(b"GET / HTTP/1.0\r\nBroken")
    for _ in range(25): assert request()[0] == 200
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool: assert all(status == 200 for status, _, _ in pool.map(lambda _: request(), range(4)))
    print("PASS: HTTP status, framing, fragmentation, errors, reuse, concurrency, and multi-segment body")
if __name__ == "__main__": main()
