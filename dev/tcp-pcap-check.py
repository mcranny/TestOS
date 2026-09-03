#!/usr/bin/env python3
"""Assert that the QEMU filter-dump capture contains a valid TCP exchange."""

import struct
import sys


def packets(path):
    with open(path, "rb") as capture:
        magic = capture.read(4)
        if magic == b"\xd4\xc3\xb2\xa1":
            endian = "<"
        elif magic == b"\xa1\xb2\xc3\xd4":
            endian = ">"
        else:
            raise AssertionError("not a classic PCAP capture")
        capture.read(20)
        while header := capture.read(16):
            _, _, included, _ = struct.unpack(endian + "IIII", header)
            frame = capture.read(included)
            if len(frame) != included or len(frame) < 54:
                continue
            if frame[12:14] != b"\x08\x00":
                continue
            ip_start = 14
            ip_len = (frame[ip_start] & 0x0F) * 4
            if frame[ip_start] >> 4 != 4 or ip_len < 20 or frame[ip_start + 9] != 6:
                continue
            tcp_start = ip_start + ip_len
            if len(frame) < tcp_start + 20:
                continue
            tcp_len = (frame[tcp_start + 12] >> 4) * 4
            if tcp_len < 20 or len(frame) < tcp_start + tcp_len:
                continue
            yield {
                "src": frame[ip_start + 12:ip_start + 16],
                "dst": frame[ip_start + 16:ip_start + 20],
                "sport": int.from_bytes(frame[tcp_start:tcp_start + 2], "big"),
                "dport": int.from_bytes(frame[tcp_start + 2:tcp_start + 4], "big"),
                "seq": int.from_bytes(frame[tcp_start + 4:tcp_start + 8], "big"),
                "ack": int.from_bytes(frame[tcp_start + 8:tcp_start + 12], "big"),
                "flags": frame[tcp_start + 13],
                "payload": frame[tcp_start + tcp_len:],
            }


def main() -> int:
    records = list(packets(sys.argv[1]))
    client_syn = next(p for p in records if p["dport"] == 12346 and p["flags"] & 0x02 and not p["flags"] & 0x10)
    server_synack = next(p for p in records if p["sport"] == 12346 and p["flags"] & 0x12 == 0x12 and p["ack"] == (client_syn["seq"] + 1) & 0xFFFFFFFF)
    next(p for p in records if p["dport"] == 12346 and p["flags"] & 0x10 and not p["flags"] & 0x02 and p["ack"] == (server_synack["seq"] + 1) & 0xFFFFFFFF)
    client_data = next(p for p in records if p["dport"] == 12346 and len(p["payload"]) == 1021)
    server_data = next(p for p in records if p["sport"] == 12346 and
                       p["dport"] == client_data["sport"] and
                       p["payload"] == client_data["payload"])
    if server_data["ack"] != (client_data["seq"] + len(client_data["payload"])) & 0xFFFFFFFF:
        raise AssertionError("guest data ACK does not cover exactly the received payload")
    next(p for p in records if p["dport"] == 12346 and p["flags"] & 0x01)
    next(p for p in records if p["sport"] == 12346 and p["flags"] & 0x01)
    print("PASS: PCAP handshake, exact data ACK, echoed payload, and FIN close")
    return 0


if __name__ == "__main__":
    sys.exit(main())
