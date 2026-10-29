# FableLauncher.exe — Compiler / Toolchain Analysis

Source binary: `D:\projects\hacking\Windows\Fable 3\FableLauncher.exe` (326,208 bytes, PE32 / i386, GUI)

## Verdict

Compiled with **Microsoft Visual C++ (MSVC) 15.00.30729 — Visual Studio 2008 SP1 (VC9)**,
linked with **Microsoft Linker 9.00.30729**, language **C++** with **LTCG (link-time code generation)**
enabled, CRT **statically linked** (import table = KERNEL32.dll only).
Authenticode-signed by **Microsoft Corporation** (timestamped, Code Signing PCA chain).

Confirmed by both **Detect It Easy v3.21** and direct PE inspection.

| Signal | Value |
|---|---|
| PE machine / subsystem | 0x014C (i386) / GUI (2) |
| PE header linker version | **9.0** → link.exe 9.00.30729 (VS2008) |
| DIE compiler signature | Microsoft Visual C/C++ (15.00.30729) [LTCG/C++] |
| DIE tool | Microsoft Visual Studio (2008) |
| OS target (PE field) | 5.0 (Windows 2000+) |
| PE timestamp | 2011-03-02 11:25:43 UTC (0x4D6E2937) |
| DllCharacteristics | 0x8140 (DYNAMIC_BASE \| NX_COMPAT \| TERMINAL_SERVER_AWARE — ASLR + /NX on) |

## Build provenance (embedded)

- **PDB path (COFF debug data):**
  `F:\dev\fable3\PCBranch\SourceCode\Tools\FableLauncher\Release\FableLauncher_SecuROM.pdb`
  → Release config, Lionhead internal build tree.
- **Version resource (VS_VERSION_INFO):**
  - CompanyName: Lionhead Studios Limited
  - FileDescription / InternalName / ProductName: Fable III Launcher
  - OriginalFilename: **FableLauncher_SecuROM.exe** (the SecuROM anti-tamper launcher)
  - FileVersion / ProductVersion: 1, 0, 0, 1
  - LegalCopyright: © 2011 Microsoft Corporation
  - LegalTrademarks: Microsoft and Fable trademarks; Lionhead Studios.
- **Codeview debug record present but no RSDS/PDB GUID + symbols stripped** (shipped without PDB).

## Sections & entropy

| Section | Size | Entropy | Notes |
|---|---|---|---|
| .text | 0x10000 | 6.62 | code |
| .rdata | 0x4200 | 5.11 | |
| .data | 0x1200 | 3.33 | |
| .rsrc | 0x39200 | **7.78** | 228 KB — almost all **icon bitmaps** (0x38808 bytes of ICON data) |
| .reloc | 0x1C00 | 4.60 | |
| overlay | 0x1B10 @0x50600 | — | Authenticode certificate table (6,928-byte WinAuth PKCS#7, signed + timestamped by Microsoft) |

- DIE's `(Heur) Packer: [Section #3 (".rsrc") compressed + High entropy]` is a **false positive**:
  the high entropy is the large high-resolution icon set, not packing. There is no packer:
  entry point is in normal `.text` (RVA 0x53EA), standard MSVC layout, no stubs, no
  .NET CLR dir, no UPX/ASPack/Themida signatures.

## CRT notes

- Import table: **KERNEL32.dll only** → static CRT (`/MT`), no msvcr*/ucrtbase dependency.
- Embedded MSVC runtime strings (from the static CRT):
  - `Microsoft Visual C++ Runtime Library`
  - `R6002 - floating point support not loaded` (CRT error banner family)
  - `Copyright (c) 1992-2004 by P.J. Plauger, licensed by Dinkumware, Ltd. ALL RIGHTS RESERVED.`
  - The "1992-2004 / Dinkumware" copyright line is an older-generation CRT string
    (the game dir also ships msvcr71.dll); the compiler/linker versions in the PE header
    and DIE signature (15.00.30729 / 9.00.30729) are the authoritative indicators → VS2008 SP1.

## Negative checks

- No Clang/LLVM, GCC, MinGW, Borland (BSJB codeview), .NET (no CLR data dir), Go, or Rust signatures.
- Not packed (see above).

## Build replication (byte-level target)

Project: `Fable 3 Decomp Project\Fable 3 Launcher\` — built with the **genuine VS2008 SP1 x86
toolchain** to reproduce the original PE as closely as possible. Build driver:
`build_vs2008.bat`; post-link fix-up: `patch_rsrc.py`.

### Toolchain (all version-15.00.30729 / 9.00.30729, x86)

- **cl.exe** 15.00.30729.01 (x86-native, from the SP1 payload) + x86 codegen backends
  (`c1.dll`, `c1xx.dll`) in `D:\Program Files (x86)\Microsoft Visual Studio 9.0\VC\bin`
- **link.exe** 9.00.30729 (x86) from the same tree
- **RC.Exe** 6.1.7600.16385 from **Windows SDK v7.1** (`C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin`)
  (the VS2008 install had no `rc.exe` / SDK headers; SDK 7.1 supplies both)
- SDK 7.1 `Include` + `Lib` provide the era-correct `windows.h` and import libs

### Compile flags (`/MT` static CRT, LTCG, Release x86)

```
cl /nologo /c /O2 /Ob2 /Oi /DNDEBUG /DWIN32 /D_WINDOWS /W3 /EHsc /GS /MT /GL /Z7 /Fds
link /nologo /LTCG /subsystem:windows /machine:x86 /base:0x400000 /DYNAMICBASE /NXCOMPAT
```

`/Z7` is used because the partial SP1 install's `mspdb80.dll` mismatches `c1.dll` and `/Zi`
trips **C1902 (PDB manager mismatch)**; `/Z7` (inline debug) and the link `/DEBUG` reproduce the
original's debug-data presence without a separate PDB.

### Resources (`.rsrc` — resource data byte-identical)

The original's `.rsrc` is reproduced from `res\icons.rc` + `res\man.rc` and then the **entire
`.rsrc` section is replaced with the original's bytes** by `patch_rsrc.py` (raw sizes match,
0x39200), which also fixes the section `VirtualSize` and the COFF `Characteristics`, and **rebases
the resource-directory RVAs** (the original's `.rsrc` sits at vaddr 0x18000; the rebuilt's is at a
different vaddr because the preceding sections are different sizes). After rebasing, all 8
resources resolve and compare byte-identical. Key details
the `.rc` had to capture exactly:

- 5 `RT_ICON` (256/64/48/16/32 px) + `RT_GROUP_ICON` at **ID 101** (not 1), language **0x0809**
- `RT_VERSION` (language **0x0809**, StringFileInfo block `080904B0`, Translation 0x0809/1200)
- `RT_MANIFEST` (asInvoker, language **0x0409**) — stored as **numeric type 24**
- Two `.res` files compiled with `/l 0x0809` and `/l 0x0409` respectively (the `LANGUAGE` statement
  ORs in 0xC000, so `/l` is required for exact language IDs)
- Version info `dwFileOS=0x4`, `dwFileFlagsMask=0x17`, `dwFileDate=0` — values `rc.exe` cannot emit,
  handled by the post-link `.rsrc` replacement

### PE header match (verified via pefile)

| Field | Rebuilt | Original | |
|---|---|---|---|
| Machine | 0x14C | 0x14C | match |
| Linker version | 9.0 | 9.0 | match |
| ImageBase | 0x400000 | 0x400000 | match |
| Section/File alignment | 0x1000 / 0x200 | 0x1000 / 0x200 | match |
| OS / subsystem version | 5.0 / 5.0 | 5.0 / 5.0 | match |
| Subsystem | GUI (2) | GUI (2) | match |
| Characteristics | 0x102 | 0x102 | match (patched) |
| DllCharacteristics | 0x8140 | 0x8140 | match |
| Sections | .text/.rdata/.data/.rsrc/.reloc | same 5 | match |
| `.rsrc` resource data | all 8 resources | all 8 resources | **byte-identical** |

The only residual `.rsrc` delta is the resource-directory RVA values themselves (they encode the
`.rsrc` vaddr). Remaining deltas are inherent to not having the original source: `.text`/`.rdata`/
`.data`/`.reloc` sizes, `AddressOfEntryPoint`, import count (57 vs 69), and the Authenticode
signature overlay. `AddressOfEntryPoint`/`SizeOfImage`/section sizes will only match once the real
source is recompiled.
