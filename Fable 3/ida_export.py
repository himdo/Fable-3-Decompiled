"""One-time IDA export of Fable3.exe's function list (size per function address).

Run by IDA in batch mode against the existing database (a copy is used so it does
not conflict with the running IDA GUI):

    idat.exe -A -S"ida_export.py" "<copy of Fable3.exe.i64>"

The "Fable 3" decomp project names functions ``sub_XXXXXX``. In the IDA
database (loaded with image base 0x400000) those names are the function's
address (VA). We therefore key the export by the function's address so the
generator can look up a size directly from a decompiled name ``sub_XXXXXX``
(decode XXXXXX -> look up key "0x......").

IDA batch mode does not reliably capture stdout/stderr, so this script writes
diagnostic markers to ``ida_export_status.txt`` (started / OK / FAIL).
"""
import json
import os
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "ida_functions.json")
STATUS = os.path.join(HERE, "ida_export_status.txt")


def _status(msg):
    try:
        with open(STATUS, "w") as fh:
            fh.write(msg)
    except Exception:
        pass


def main():
    import idc
    import ida_funcs
    import idautils

    funcs = {}
    n = 0
    for start in idautils.Functions():
        f = ida_funcs.get_func(start)
        if f is None:
            continue
        size = int(f.end_ea - start)
        if size <= 0:
            continue
        name = idc.get_func_name(start)
        # Key by the function's address (VA in the database). The decompiled
        # name sub_XXXXXX decodes to this same address.
        funcs["0x%08x" % int(start)] = {
            "size": size,
            "name": name,
        }
        n += 1

    out = {
        "imagebase": "0x400000",
        "count": n,
        "keyed_by": "ea",
        "functions": funcs,
    }
    with open(OUT, "w") as fh:
        json.dump(out, fh, indent=2)
    _status("OK %d functions -> %s" % (n, OUT))


try:
    _status("STARTED (loading db...)")
    main()
except Exception as e:
    tb = traceback.format_exc()
    _status("FAIL %r\n%s" % (e, tb))
    raise SystemExit(1)
