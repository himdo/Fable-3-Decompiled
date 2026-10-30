#!/usr/bin/env python3
"""Post-link patch: make FableLauncher.exe's .text byte-identical to the
original FableLauncher.exe, and set the entry point to the original's, so the
original's WinMain (and its CRT/.rdata references) work.

Usage: patch_f3blob.py <exe>
"""
import struct, sys, os

ORIG  = r"D:\projects\hacking\Windows\Fable 3\FableLauncher.exe"
TEXT_VA = 0x401000
ORIG_ENTRY = 0x4053EA  # original WinMainCRTStartup

def parse_pe(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    ohsz = struct.unpack_from("<H", d, pe + 20)[0]
    sto  = pe + 24 + ohsz
    secs = []
    for i in range(nsec):
        off = sto + i * 40
        name = d[off:off+8].rstrip(b"\x00").decode("ascii", "replace")
        vsize, rva, rawsize, rawptr = struct.unpack_from("<IIII", d, off + 8)
        secs.append([name, rva, vsize, rawptr, rawsize, off])
    return d, pe, secs

def rva2off(rva, secs):
    for name, r, vs, rp, rsz, _ in secs:
        if r <= rva < r + max(vs, rsz, 1):
            return rp + (rva - r)
    return None

def main():
    exe = sys.argv[1]
    d, pe, secs = parse_pe(exe)
    o, _, osecs = parse_pe(ORIG)

    # --- .text: copy original's bytes + vsize ---
    t  = [s for s in secs  if s[0] == ".text"][0]
    ot = [s for s in osecs if s[0] == ".text"][0]
    text_off = t[3]                 # new exe .text file offset (raw ptr)
    orig_text_bytes = o[ot[3]:ot[3]+ot[2]]   # original .text (vsize bytes)
    # ensure it fits in the new .text raw data
    assert len(orig_text_bytes) <= t[4], "orig .text bigger than new .text raw"
    d = bytearray(d)
    d[text_off:text_off+len(orig_text_bytes)] = orig_text_bytes
    # set new .text vsize = original vsize
    struct.pack_into("<I", d, t[5] + 8, ot[2])
    print("  .text: copied %d bytes (vsize now 0x%X)" % (len(orig_text_bytes), ot[2]))

    # --- entry point -> original ---
    ep_off = pe + 24 + 16           # optional header AddressOfEntryPoint (PE32)
    struct.pack_into("<I", d, ep_off, ORIG_ENTRY)
    print("  entry point -> 0x%X" % ORIG_ENTRY)

    open(exe, "wb").write(bytes(d))
    print("PATCH OK (text + entry)")

main()
