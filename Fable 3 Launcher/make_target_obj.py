#!/usr/bin/env python3
"""Build a target COFF x86 object file (orig/main.obj) from the original
FableLauncher.exe, so objdiff (which only reads ELF/COFF object files) can
compare the rebuilt main.obj against the original.

.text / .xdata / .rdata / .data are copied verbatim from the original PE.
If orig/functions.json exists, function symbols are added to .text so
objdiff can diff per-function. Format:
    [ {"name": "wmain", "rva": 4198480, "end": 4198600}, ... ]

Layouts follow the `object` crate (which objdiff uses): 18-byte symbol
records, aux-function total_size at offset 4, section symbols static.
Usage: python make_target_obj.py [original_exe] [out_obj]
"""

import json
import struct
import sys
from pathlib import Path

MACHINE_I386 = 0x014C
FILE_RELOCATABLE = 0x01
FILE_32BIT_MACHINE = 0x0100
# COFF storage classes (IMAGE_SYM_CLASS_*): EXTERNAL=2, STATIC=3.
# EXTERNAL so objdiff (object crate) reads the aux-function total_size for each
# function; STATIC for section symbols (aux section record, typ=0, naux>0).
STORAGE_EXTERNAL = 0x02
STORAGE_STATIC = 0x03
TYPE_FUNCTION = 0x21   # derived FUNCTION (0x20) | INT
TYPE_DATA = 0x31

ALIGN = {b".text": 4, b".xdata": 4, b".rdata": 4, b".data": 4}
CHARS = {b".text": 0x60, b".xdata": 0x40, b".rdata": 0x40, b".data": 0x40}
# .text = CODE|DATA, others = DATA (matches real MSVC .obj section characteristics)


def align_up(v: int, a: int) -> int:
    return (v + a - 1) // a * a


class Section:
    def __init__(self, name: bytes, data: bytes, align: int, chars: int):
        self.name = name
        self.data = data
        self.align = align
        self.chars = chars
        # (offset, size, name) function/data symbols
        self.symbols: list[tuple[int, int, str]] = []


def build_coff(sections: list[Section]) -> bytes:
    # ---- symbols: {name8|name_str, value, secnum, typ16, storage, naux, aux} ----
    syms: list[dict] = []
    syms.append({"name8": b"\x00" * 8, "value": 0, "secnum": 0,
                 "typ16": 0, "storage": 0, "naux": 0})  # null symbol

    for si, s in enumerate(sections):
        # Section symbol: static, type 0, 1 aux (length/reloc/linenums).
        # The name field must carry the section name; the high byte of the
        # section-number field carries the section characteristics (MSVC style).
        syms.append({"name8": s.name[:8].ljust(8, b"\x00"), "value": 0,
                     "secnum": (s.chars & 0xFF) << 8,
                     "typ16": 0, "storage": STORAGE_STATIC, "naux": 1,
                     "aux": struct.pack("<IHHIHBBH", len(s.data), 0, 0, 0, 0, 0, 0, 0)})
        for offset, size, name in s.symbols:
            nm = name.encode("latin-1", "replace")
            sym = {"value": offset, "secnum": si + 1,
                   "typ16": TYPE_FUNCTION if s.name == b".text" else TYPE_DATA,
                   "storage": STORAGE_EXTERNAL,
                   "naux": 1 if s.name == b".text" else 0,
                   "aux": struct.pack("<IIII2s", 0x02, size, 0, 0, b"") if s.name == b".text" else b""}
            if len(nm) <= 8:
                sym["name8"] = nm.ljust(8, b"\x00")
            else:
                # long name: name[0] must be 0, name[4:8] = string-table offset
                sym["name_str"] = nm
                sym["name8"] = b"\x00" + nm[:3] + b"\x00\x00\x00\x00"  # offset patched later
            syms.append(sym)

    # ---- string table (first 4 bytes = total size including themselves) ----
    str_off = 4
    strtable = bytearray(b"\x00\x00\x00\x00")
    for sym in syms:
        if "name_str" in sym:
            sym["str_off"] = str_off
            strtable += sym["name_str"] + b"\x00"
            str_off += len(sym["name_str"]) + 1
            sym["name8"] = b"\x00" + sym["name_str"][:3] + struct.pack("<I", sym["str_off"])
    str_size = str_off  # total size including the 4-byte length header
    struct.pack_into("<I", strtable, 0, str_size)
    strtable += b"\x00" * (align_up(str_size, 4) - str_size)

    # ---- file layout ----
    nsec = len(sections)
    # symbol record count includes aux records
    n_symrecs = sum(1 + s["naux"] for s in syms)
    symtab_size = 18 * n_symrecs
    symtab_off = 20 + 40 * nsec
    strtab_off = symtab_off + symtab_size
    file_ptr = strtab_off + len(strtable)

    file_header = struct.pack("<HHIIIHH", MACHINE_I386, nsec, 0, symtab_off,
                              n_symrecs, 0, FILE_RELOCATABLE | FILE_32BIT_MACHINE)

    sec_headers = b""
    for s in sections:
        sec_headers += struct.pack(
            "<8sIIIIIIHHI",
            s.name.ljust(8, b"\x00"),
            0,              # physical address
            0,              # virtual address
            len(s.data),    # size of raw data
            file_ptr,       # pointer to raw data
            0, 0,           # reloc / linenumber pointers
            0, 0,           # reloc / linenumber counts
            s.chars,
        )
        file_ptr += align_up(len(s.data), s.align)

    symtab = bytearray()
    for sym in syms:
        symtab += struct.pack("<8sIH H BB".replace(" ", ""),
                              sym["name8"], sym["value"], sym["secnum"],
                              sym["typ16"], sym["storage"], sym["naux"])
        for _ in range(sym["naux"]):
            symtab += sym.get("aux", b"\x00" * 18)

    body = bytearray(sec_headers) + bytearray(symtab) + bytearray(strtable)
    for s in sections:
        body += s.data
        body += b"\x00" * (align_up(len(s.data), s.align) - len(s.data))

    return bytes(file_header + body)


def main() -> int:
    here = Path(__file__).resolve().parent
    exe = Path(sys.argv[1]) if len(sys.argv) > 1 else here / "orig" / "FableLauncher.exe"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else here / "orig" / "main.obj"
    if not exe.exists():
        print(f"make_target_obj: missing {exe}", file=sys.stderr)
        return 1

    data = exe.read_bytes()
    if data[:2] != b"MZ":
        print(f"make_target_obj: {exe} is not a PE file", file=sys.stderr)
        return 1
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_off:pe_off + 4] != b"PE\0\0":
        print(f"make_target_obj: bad PE signature", file=sys.stderr)
        return 1
    machine, nsecs = struct.unpack_from("<HH", data, pe_off + 4)
    if machine != MACHINE_I386:
        print(f"make_target_obj: unexpected machine {machine:#x}", file=sys.stderr)
        return 1
    # This binary has a non-standard PE file header (patched), so don't trust the
    # SizeOfOptionalHeader offset. Locate the section table by scanning for the
    # first offset where nsecs consecutive 40-byte headers all carry a valid
    # ".name" section-name field.
    sec_table = None
    for base in range(pe_off + 24, min(pe_off + 0x400, len(data) - 40 * nsecs), 4):
        ok = True
        for i in range(nsecs):
            nm = data[base + 40 * i: base + 40 * i + 8]
            if not nm[:1] == b"." or b"\x00" in nm[:1] or not all(32 <= c < 127 for c in nm.rstrip(b"\x00")):
                ok = False
                break
        if ok:
            sec_table = base
            break
    if sec_table is None:
        print("make_target_obj: could not locate the PE section table", file=sys.stderr)
        return 1

    want = [b".text", b".xdata", b".rdata", b".data"]
    sections: list[Section] = []
    text_va = None
    for i in range(nsecs):
        off = sec_table + 40 * i
        name = data[off:off + 8].rstrip(b"\x00")
        vsz, va, rawsz, rawoff = struct.unpack_from("<IIII", data, off + 8)
        if name == b".text":
            text_va = va
        if name in want:
            # Copy only the real content (vsz), not file-alignment padding
            size = min(vsz, rawsz) if vsz else rawsz
            sections.append(Section(name, data[rawoff:rawoff + size],
                                    ALIGN[name], CHARS[name]))

    # ---- optional function symbols from orig/functions.json ----
    fn_count = 0
    fns_file = out.parent / "functions.json"
    text_idx = next((i for i, s in enumerate(sections) if s.name == b".text"), None)
    if fns_file.exists() and text_idx is not None and text_va is not None:
        fns = json.loads(fns_file.read_text(encoding="utf-8"))
        for fn in fns:
            rva = fn["rva"]
            end = fn.get("end", rva + fn.get("size", 0))
            sections[text_idx].symbols.append((rva - text_va, end - rva, fn["name"]))
        fn_count = len(fns)

    blob = build_coff(sections)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    print(f"make_target_obj: wrote {out} ({len(blob)} bytes, "
          f"sections={[s.name.decode() for s in sections]}, function symbols={fn_count})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
