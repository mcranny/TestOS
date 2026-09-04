#!/usr/bin/env python3
"""Normalize FAT directory-entry timestamps for reproducible image artifacts."""
import struct
import sys

FAT_EOC = 0x0FFFFFF8
# 1980-01-01 00:00:00, a valid FAT timestamp.
FAT_DATE = 0x0021
FAT_TIME = 0x0000


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def main(path):
    with open(path, "rb") as image:
        data = bytearray(image.read())

    if data[82:90] != b"FAT32   ":
        raise SystemExit("expected a FAT32 image")

    bytes_per_sector = u16(data, 11)
    sectors_per_cluster = data[13]
    reserved_sectors = u16(data, 14)
    fat_count = data[16]
    sectors_per_fat = u32(data, 36)
    root_cluster = u32(data, 44)
    fat_offset = reserved_sectors * bytes_per_sector
    data_offset = (reserved_sectors + fat_count * sectors_per_fat) * bytes_per_sector
    cluster_size = bytes_per_sector * sectors_per_cluster

    def cluster_offset(cluster):
        return data_offset + (cluster - 2) * cluster_size

    def next_cluster(cluster):
        return u32(data, fat_offset + cluster * 4) & 0x0FFFFFFF

    def walk_directory(cluster, visited):
        if cluster < 2 or cluster in visited:
            return
        visited.add(cluster)
        while cluster < FAT_EOC:
            start = cluster_offset(cluster)
            for entry in range(start, start + cluster_size, 32):
                first = data[entry]
                if first == 0x00:
                    return
                if first == 0xE5 or data[entry + 11] == 0x0F:
                    continue
                struct.pack_into("<H", data, entry + 14, FAT_TIME)
                struct.pack_into("<H", data, entry + 16, FAT_DATE)
                struct.pack_into("<H", data, entry + 18, FAT_DATE)
                struct.pack_into("<H", data, entry + 22, FAT_TIME)
                struct.pack_into("<H", data, entry + 24, FAT_DATE)
                if data[entry + 11] & 0x10 and data[entry] != ord("."):
                    child = (u16(data, entry + 20) << 16) | u16(data, entry + 26)
                    walk_directory(child, visited)
            cluster = next_cluster(cluster)

    walk_directory(root_cluster, set())
    with open(path, "wb") as image:
        image.write(data)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: normalize-fat-timestamps.py IMAGE")
    main(sys.argv[1])
