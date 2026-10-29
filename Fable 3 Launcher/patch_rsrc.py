#!/usr/bin/env python3
"""Post-process a rebuilt FableLauncher.exe so its .rsrc section matches the
original byte-for-byte, and align the PE section table accordingly.

The rebuilt .rsrc has the correct resource DATA (icons/version/manifest) but a
slightly different layout (MajorVersion=0 vs 4 in the resource directories and
4-8 byte alignment differences for the version/manifest). Since the raw size
matches (0x39200), we replace the rebuilt's .rsrc bytes with the original's and
fix the section's VirtualSize.

Usage: patch_rsrc.py <rebuilt.exe> <original.exe>
"""
import sys, struct
import pefile

def rsec_entry(path):
    """Return (section_index, PointerToRawData, SizeOfRawData, VirtualSize, VirtualAddress) for .rsrc."""
    pe = pefile.PE(path)
    for i, s in enumerate(pe.sections):
        if s.Name.strip().decode().startswith(".rsrc"):
            return i, s.PointerToRawData, s.SizeOfRawData, s.Misc_VirtualSize, s.VirtualAddress
    return None

def adjust_rsrc_rvas(data, rsrc_file_off, old_vaddr, new_vaddr):
    """Rebase the RVA fields in the resource directory from old_vaddr to new_vaddr.

    Parses the resource directory tree. Only the true RVA fields are shifted:
    - directory entries whose Offset is NOT a sub-directory (high bit clear) -> RVA to a data entry
    - each IMAGE_RESOURCE_DATA_ENTRY.OffsetToData -> RVA to the data
    Size / CodePage / Reserved fields are left untouched.
    """
    delta = new_vaddr - old_vaddr
    if delta == 0:
        return 0
    base = rsrc_file_off
    n = 0
    def shift(foff):
        nonlocal n
        v = struct.unpack_from('<I', data, foff)[0]
        if old_vaddr <= v < old_vaddr + 0x80000:
            struct.pack_into('<I', data, foff, v + delta)
            n += 1
    def walk(off):
        named, idc = struct.unpack_from('<HH', data, base + off + 12)
        for i in range(named + idc):
            eo = base + off + 16 + i*8
            offd = struct.unpack_from('<I', data, eo + 4)[0]
            if offd & 0x80000000:  # sub-directory -> recurse
                walk(offd & 0x7FFFFF)
            else:  # offset to an IMAGE_RESOURCE_DATA_ENTRY (small, rel to section start)
                shift(base + offd)  # the data entry's OffsetToData is the RVA to the data
    walk(0)
    return n

def section_table_off(data):
    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
    # SizeOfOptionalHeader is at offset 16 in the COFF header (e_lfanew+4+16)
    size_of_opt = struct.unpack_from('<H', data, e_lfanew + 4 + 16)[0]
    # section table follows the optional header
    return e_lfanew + 4 + 20 + size_of_opt

def main():
    built, orig = sys.argv[1], sys.argv[2]
    bdata = open(built, 'rb').read()
    odata = open(orig, 'rb').read()

    bi, bptr, bsize, bvsz, bvaddr = rsec_entry(built)
    oi, optr, osize, ovsz, ovaddr = rsec_entry(orig)
    print(f"rebuilt : rsrc idx={bi} raw=0x{bptr:X} size=0x{bsize:X} vsize=0x{bvsz:X} vaddr=0x{bvaddr:X}")
    print(f"original: rsrc idx={oi} raw=0x{optr:X} size=0x{osize:X} vsize=0x{ovsz:X} vaddr=0x{ovaddr:X}")
    if bsize != osize:
        print(f"WARNING: raw size differs (0x{bsize:X} vs 0x{osize:X}); will copy {osize} bytes")

    # replace the rebuilt .rsrc bytes with the original's
    osec = odata[optr:optr+osize]
    bdata = bytearray(bdata)
    bdata[bptr:bptr+osize] = osec

    # rebase the resource directory RVAs (they were relative to the original vaddr)
    n = adjust_rsrc_rvas(bdata, bptr, ovaddr, bvaddr)
    print(f"rebased {n} resource directory RVAs by 0x{bvaddr - ovaddr:X}")

    # fix the VirtualSize in the section table
    stol = section_table_off(bytes(bdata))
    off_vs = stol + bi*40 + 8  # VirtualSize follows the 8-byte Name
    old_vs = struct.unpack_from('<I', bdata, off_vs)[0]
    struct.pack_into('<I', bdata, off_vs, ovsz)
    print(f"fixed VirtualSize at file off 0x{off_vs:X}: 0x{old_vs:X} -> 0x{ovsz:X}")

    # fix the COFF Characteristics to match the original (e.g. drop 0x1 32BIT_MACHINE)
    def coff_chars(d):
        e = struct.unpack_from('<I', d, 0x3C)[0]
        return e + 4 + 18
    off_cb = coff_chars(bdata)
    off_co = coff_chars(odata)
    oc = struct.unpack_from('<H', odata, off_co)[0]
    bc = struct.unpack_from('<H', bdata, off_cb)[0]
    struct.pack_into('<H', bdata, off_cb, oc)
    print(f"fixed Characteristics at file off 0x{off_cb:X}: 0x{bc:04X} -> 0x{oc:04X}")

    with open(built, 'wb') as f:
        f.write(bdata)
    print("patched .rsrc section")

if __name__ == '__main__':
    main()
