#!/usr/bin/env python3
"""Patch a specific PE resource's data in a rebuilt exe with the exact bytes
from the original exe. Used to force the VS_VERSION_INFO to match the original
byte-for-byte (the rc.exe cannot emit the original dwFileFlagsMask / file-date).

Usage: patch_resource.py <rebuilt.exe> <original.exe> <type> [id] [lang]
"""
import sys
import pefile

def find_resource_bytes(data, rtype, rid, lang):
    """Parse PE from in-memory bytes; return (file_offset, size) of a resource."""
    pe = pefile.PE(data=data)
    res = pe.DIRECTORY_ENTRY_RESOURCE
    for t in res.entries:
        tid = t.id if t.id is not None else None
        if tid != rtype:
            continue
        for n in t.directory.entries:
            nid = n.id if n.id is not None else None
            if nid != rid:
                continue
            for l in n.directory.entries:
                if lang is not None and l.id != lang:
                    continue
                d = l.data
                if isinstance(d, list):
                    d = d[0]
                off = d.struct.OffsetToData
                size = d.struct.Size
                for s in pe.sections:
                    if s.VirtualAddress <= off < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
                        return s.PointerToRawData + (off - s.VirtualAddress), size
    return None

def main():
    built, orig, rtype, rid = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    lang = int(sys.argv[5]) if len(sys.argv) > 5 else None

    with open(orig, 'rb') as f:
        orig_all = f.read()
    src = find_resource_bytes(orig_all, rtype, rid, lang)
    if src is None:
        print(f"resource {rtype}/{rid} not found in original"); sys.exit(1)
    soff, ssize = src
    orig_bytes = orig_all[soff:soff+ssize]

    with open(built, 'rb') as f:
        built_all = bytearray(f.read())
    dst = find_resource_bytes(bytes(built_all), rtype, rid, lang)
    if dst is None:
        print(f"resource {rtype}/{rid} not found in rebuilt"); sys.exit(1)
    doff, dsize = dst

    print(f"original: file_off=0x{soff:X} size={ssize}")
    print(f"rebuilt : file_off=0x{doff:X} size={dsize}")
    if ssize != dsize:
        print("WARNING: size mismatch; will still overwrite")
    built_all[doff:doff+ssize] = orig_bytes
    with open(built, 'wb') as f:
        f.write(built_all)
    print(f"patched {len(orig_bytes)} bytes at file offset 0x{doff:X}")

if __name__ == '__main__':
    main()
