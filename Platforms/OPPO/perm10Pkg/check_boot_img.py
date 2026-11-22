#!/usr/bin/env python3
"""Sanity-check the Android boot image produced for PERM10.

The image must be loadable by ABL and contain the PERM10 UEFI FD behind the
BootShim. This verifies the container, the shim's copy target and the FD size;
it says nothing about whether the firmware boots.
"""
import argparse
import struct
import zlib
from pathlib import Path

FD_MAGIC_OFFSET = 0x28


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--fd-base", default="0x9FC00000")
    parser.add_argument("--fd-size", default="0x300000")
    args = parser.parse_args()

    blob = args.image.read_bytes()
    if blob[:8] != b"ANDROID!":
        raise SystemExit("not an Android boot image")
    kernel_size = struct.unpack_from("<I", blob, 8)[0]
    kernel_addr = struct.unpack_from("<I", blob, 12)[0]
    ramdisk_size = struct.unpack_from("<I", blob, 16)[0]
    ramdisk_addr = struct.unpack_from("<I", blob, 20)[0]
    page_size = struct.unpack_from("<I", blob, 36)[0]
    if page_size & (page_size - 1) or page_size < 0x800:
        raise SystemExit(f"implausible page size {page_size:#x}")
    header_version = struct.unpack_from("<I", blob, 40)[0] if len(blob) > 44 else 0
    print(
        f"header v{header_version}, page {page_size:#x}, "
        f"kernel {kernel_size:#x}@{kernel_addr:#x}, ramdisk {ramdisk_size:#x}@{ramdisk_addr:#x}"
    )

    kernel = blob[page_size : page_size + kernel_size]
    if kernel[:2] != b"\x1f\x8b":
        raise SystemExit("kernel payload is not gzip compressed")
    payload = zlib.decompressobj(31).decompress(kernel)
    print(f"decompressed payload {len(payload):#x} bytes")

    expected_base = int(args.fd_base, 16)
    expected_size = int(args.fd_size, 16)
    shim_base, shim_size = struct.unpack_from("<2Q", payload, 8)
    if shim_base != expected_base or shim_size != expected_size:
        raise SystemExit(
            f"shim copies {shim_base:#x}+{shim_size:#x}, expected {expected_base:#x}+{expected_size:#x}"
        )
    print(f"shim copy target {shim_base:#x}+{shim_size:#x}")

    fv = payload.find(b"_FVH", 0x40, 0x100)
    if fv < 0:
        raise SystemExit("no firmware volume header after the shim")
    fv_offset = fv - FD_MAGIC_OFFSET
    length = struct.unpack_from("<Q", payload, fv_offset + 0x20)[0]
    print(f"FD at {fv_offset:#x}, FV length {length:#x}")
    if length != expected_size:
        raise SystemExit(f"FV length {length:#x} differs from the declared FD size {expected_size:#x}")
    if shim_base + shim_size > 0x9FF00000:
        raise SystemExit("shim target leaves the PERM10 UEFI FD window")
    print("OK: container, shim target and FD size are consistent")


if __name__ == "__main__":
    main()
