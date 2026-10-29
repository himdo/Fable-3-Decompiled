# Fable 3 Launcher — Byte-Identical Build Replication

This project rebuilds the original **`FableLauncher.exe`** (the SecuROM launcher shipped in
Fable 3) using the **genuine Visual Studio 2008 SP1 x86 toolchain**, so the output matches the
original as closely as is possible without the original source code.

The original was compiled with:

| Item | Value |
|---|---|
| Compiler | MSVC 15.00.30729.01 (VS2008 SP1, x86) |
| Linker | 9.00.30729 (LTCG) |
| Language | C++, static CRT (`/MT`), `/EHsc`, Release `/O2` |
| Subsystem | Windows GUI (x86), image base `0x400000` |
| Target OS | Windows 5.0 (Vista era) |

See `../launcher/compiler-analysis.md` for the full reverse-engineering / compiler-identification
write-up.

---

## What is required to match the original byte-for-byte

Matching the original is a **three-part problem**. Each part must be satisfied or a field of the
PE will differ.

### 1. The exact toolchain (version-locked)

The compiler and linker **versions** are embedded in the PE header (`LinkerVersion 9.0`, MSVC
build ID). A different compiler — even another MSVC — writes different bytes. You need:

- **`cl.exe` 15.00.30729.01** — the **x86-native** compiler driver **plus its matching x86
  codegen backends** (`c1.dll`, `c1xx.dll`) from VS2008 SP1.
  - The x64/x86_amd64 drivers will **not** work: the driver is ABI-locked to the codegen backends
    in its own directory, and the x86-targeting backends differ from the x64-targeting ones.
  - A partial VS2008 install is often missing the x86 `cl.exe` / `c1.dll` / `c1xx.dll` in
    `VC\bin`. Recover them from the SP1 payload (`VC90sp1-KB947888-x86-enu.msp`).
- **`link.exe` 9.00.30729** (x86) from the same tree, plus its x86 helpers (`msobj80.dll`,
  `mspdb80.dll`).
- **`rc.exe` 6.1.7600.16385** from **Windows SDK v7.1** (the VS2008 install ships no `rc.exe`).
- **Windows SDK v7.1** `Include` + `Lib` — the era-correct `windows.h` and import libraries
  (e.g. `kernel32.lib`). Modern Windows Kits headers produce subtly different import tables.

> Expected install locations (as used by this build):
> - VS2008 SP1: `D:\Program Files (x86)\Microsoft Visual Studio 9.0`
> - Windows SDK 7.1: `C:\Program Files\Microsoft SDKs\Windows\v7.1`

**Debug info:** build with **`/Z7`** (inline debug), not `/Zi`. The partial install's
`mspdb80.dll` mismatches `c1.dll`, so `/Zi` trips **C1902 (PDB manager mismatch)**. `/Z7` plus
the link-time `/DEBUG` reproduces the original's debug-data presence without an external PDB.

### 2. The exact compile / link flags

```
cl   /nologo /c /O2 /Ob2 /Oi /DNDEBUG /DWIN32 /D_WINDOWS /W3 /EHsc /GS /MT /GL /Z7 /Fds
link /nologo /LTCG /subsystem:windows /machine:x86 /base:0x400000 /DYNAMICBASE /NXCOMPAT
```

- `/MT` — static CRT (the original has no CRT DLL import; only `KERNEL32.dll`).
- `/GL` + `/LTCG` — whole-program / link-time code generation (confirmed by DIE + PE).
- `/EHsc` — synchronous C++ exceptions (note: the original has **no `.pdata` section**, so the
  real source must generate no unwind tables — a build detail to verify against the real code).
- `/base:0x400000` — fixed image base.
- `/DYNAMICBASE` + `/NXCOMPAT` — produce the original's `DllCharacteristics = 0x8140`
  (DYNAMIC_BASE `0x40` | NX_COMPAT `0x100` | TERMINAL_SERVER_AWARE `0x8000`) **and** the `.reloc`
  section. Without `/DYNAMICBASE` the `.reloc` section is absent.
- `/subsystem:windows` — GUI subsystem (entry point `WinMainCRTStartup`).

These flags yield the original's PE shape: **5 sections** —
`.text` / `.rdata` / `.data` / `.rsrc` / `.reloc` — with `Machine 0x14C`,
`Characteristics 0x102`, `SectionAlignment 0x1000`, `FileAlignment 0x200`, `OSVersion 5.0`.

### 3. The exact resource section (`.rsrc`)

The original's `.rsrc` (raw size `0x39200`) contains exactly 8 resources, all of which must match:

| Type | ID | Language | Notes |
|---|---|---|---|
| `RT_ICON` | 1–5 | `0x0809` | 256 / 64 / 48 / 16 / 32 px icon bitmaps |
| `RT_GROUP_ICON` | **101** | `0x0809` | group icon — note the non-standard ID **101**, not 1 |
| `RT_VERSION` | 1 | `0x0809` | `StringFileInfo` block `080904B0`, `Translation 0x0809/1200` |
| `RT_MANIFEST` | 1 | `0x0409` | standard `asInvoker` XML |

Non-obvious details that a default rebuild gets wrong:

- **Mixed resource languages** — icons/version use `0x0809`, the manifest uses `0x0409`.
- **`rc.exe`'s `LANGUAGE` statement is unusable here** — it ORs in `0xC000` (e.g. `0x0809` →
  `0xC809`). The exact language must be set with the **`/l 0x<lang>`** command-line option, which
  means compiling the icons/version and the manifest as **two separate `.res` files** at two
  different `/l` values.
- **Numeric resource types** — the original stores types as numeric IDs (3, 14, 16, 24). Using
  `rc.exe` keyword names (`RT_VERSION`, `RT_MANIFEST`) stores them as *named* entries, which
  changes the resource-directory layout (vsize +44 bytes). Use numeric syntax, e.g. `1 24
  "manifest.xml"`.
- **Version-info fields `rc.exe` cannot emit** — the original's `VS_FIXEDFILEINFO` has
  `dwFileOS = 0x4` (not the usual `0x40004`), `dwFileFlagsMask = 0x17`, and `dwFileDate = 0`.
  `rc.exe` rejects `FILEFLAGS_MASK` and forces `dwFileDateMS = 1`, so the version resource is
  corrected by the post-link patch (below).
- **Source encoding** — the `.rc` is read as **cp1252** (the system codepage), not UTF-8. The
  `©` in `LegalCopyright` must be byte `0xA9` in the file, or it expands to two characters.

### 4. The post-link `.rsrc` replacement (`patch_rsrc.py`)

Even with the above, the rebuilt `.rsrc` won't be byte-identical because the VS2008 linker
applies different alignment to the version/manifest data and emits `MajorVersion = 0` in the
resource directories (the original has `MajorVersion = 4`). Since the **raw sizes match**
(`0x39200`), `patch_rsrc.py` **replaces the entire rebuilt `.rsrc` with the original's bytes**,
then:

1. **Rebases the resource-directory RVAs** — the original's `.rsrc` sits at vaddr `0x18000`; the
   rebuilt's sits elsewhere because the code sections are a different size. Only the true RVA
   fields (data-entry offsets + each `OffsetToData`) are shifted; sizes are left intact.
2. **Fixes the `.rsrc` `VirtualSize`** in the PE section table.
3. **Copies the COFF `Characteristics`** from the original (drops the `0x1 32BIT_MACHINE` bit the
   modern build sets).

After this, all 8 resources resolve and compare **byte-identical** to the original. The only
residual `.rsrc` difference is the resource-directory RVA values themselves, which encode the
`.rsrc` vaddr.

---

## What will *not* match (and why)

Without the original source, these fields differ — they are all **code-dependent**:

- `.text` / `.rdata` / `.data` / `.reloc` sizes, `SizeOfImage`, `AddressOfEntryPoint`
- `KERNEL32.dll` import count (stub `main.cpp` pulls fewer imports than the real launcher)
- The Authenticode signature overlay (the original is Microsoft-signed)

These resolve only once the real source is decompiled and recompiled. The goal of this project is
to make the **build environment, resources, and PE structure** match, so that a faithful
recompile of the source drops into a byte-identical frame.

---

## How to run a build

### Prerequisites

1. **VS2008 SP1 (x86)** installed, with the x86-native `cl.exe` + codegen backends present in
   `VC\bin`. Verify:
   ```
   "D:\Program Files (x86)\Microsoft Visual Studio 9.0\VC\bin\cl.exe"
   ```
   should print `Microsoft (R) 32-bit C/C++ Optimizing Compiler Version 15.00.30729.01 for 80x86`.
2. **Windows SDK v7.1** installed (`Include` + `Lib` present).
3. **Python 3** with `pefile` installed (`pip install pefile`) — required by `patch_rsrc.py`.
4. The **original** `FableLauncher.exe` at the path referenced by `ORIG` in `build_vs2008.bat`.

> If your toolchain lives elsewhere, edit the `V90`, `SDK`, and `ORIG` variables at the top of
> `build_vs2008.bat`.

### Build

From this folder:

```
build_vs2008.bat
```

This runs the full pipeline:

1. `python extract_icons.py <original> res` — extract the copyrighted icon assets (5 images →
   `fable.ico` + `icon1..5.bin` + `groupicon.bin`) from the original; these are **not** committed
   to the repo
2. `rc.exe /l 0x0809 …  icons.res  icons.rc` — icons + version (language `0x0809`)
3. `rc.exe /l 0x0409 …  man.res    man.rc`  — manifest (language `0x0409`)
4. `cl  …  main.cpp`                        — compile (x86)
5. `link … FableLauncher.exe  main.obj  res\icons.res  res\man.res` — link (LTCG, x86)
6. `python patch_rsrc.py FableLauncher.exe <original>` — byte-match the `.rsrc` section

Output: **`Release\Win32\FableLauncher.exe`**

A successful build prints `=== BUILD OK: …\Release\Win32\FableLauncher.exe ===`.

### Verify

```
python - <<'PY'
import pefile
reb = pefile.PE("Release/Win32/FableLauncher.exe")
org = pefile.PE(r"D:\projects\hacking\Windows\Fable 3\FableLauncher.exe")
for a,b in [("Machine",reb.FILE_HEADER.Machine,org.FILE_HEADER.Machine),
            ("Linker", (reb.OPTIONAL_HEADER.MajorLinkerVersion,reb.OPTIONAL_HEADER.MinorLinkerVersion),
                        (org.OPTIONAL_HEADER.MajorLinkerVersion,org.OPTIONAL_HEADER.MinorLinkerVersion)),
            ("ImageBase",reb.OPTIONAL_HEADER.ImageBase,org.OPTIONAL_HEADER.ImageBase),
            ("Characteristics",reb.FILE_HEADER.Characteristics,org.FILE_HEADER.Characteristics),
            ("DllCharacteristics",reb.OPTIONAL_HEADER.DllCharacteristics,org.OPTIONAL_HEADER.DllCharacteristics),
            ("Sections",len(reb.sections),len(org.sections))]:
    print(f"{a:18} rebuilt={b[0]} original={b[1]} match={b[0]==b[1]}")
PY
```

Expect every field to report `match=True` (except, at the byte level, the code-dependent section
sizes / entry point noted above).

---

## File layout

```
Fable 3 Launcher\
├── build_vs2008.bat          # build driver (toolchain, flags, pipeline)
├── extract_icons.py          # extract icon assets from the original (not committed)
├── patch_rsrc.py             # post-link: byte-match the .rsrc section
├── main.cpp                  # stub WinMain (replace with decompiled source when available)
├── Fable 3 Launcher.vcxproj  # VS project documenting the v90 toolchain + resources
├── res\
│   ├── fable.ico             # 5-image icon (256/64/48/16/32 px)  [GENERATED — gitignored]
│   ├── icon1..5.bin          # raw RT_ICON extractions             [GENERATED — gitignored]
│   ├── groupicon.bin         # raw RT_GROUP_ICON extraction        [GENERATED — gitignored]
│   ├── icons.rc              # 101 ICON + VERSIONINFO (language 0x0809)  — cp1252 encoded
│   ├── man.rc                # 1 24 "manifest.xml"   (language 0x0409)
│   ├── manifest.xml          # asInvoker manifest
│   └── versioninfo.bin       # raw RT_VERSION reference extraction
└── Release\Win32\            # build output (FableLauncher.exe + .pdb + .obj)
```

> The icon assets (`fable.ico`, `icon1..5.bin`, `groupicon.bin`) are copyrighted and are
> **regenerated from the original binary** by `extract_icons.py` (run automatically at the start of
> the build). They are gitignored and must not be committed; `extract_icons.py` rebuilds them
> byte-for-byte.

> **Note on `main.cpp`:** it is currently a minimal `WinMain` stub so the build produces a
> structurally correct PE. To get a true byte-identical `.text`, replace it with the decompiled
> launcher source (and re-derive the exact import set / entry point).
