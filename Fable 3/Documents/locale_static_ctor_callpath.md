# Fable 3 — How the locale-string functions are called from program start

Reverse-engineering notes for `Fable3.exe` (x86 PE32, image base `0x00400000`,
GUI subsystem, symbols stripped). Documents the **direct call path** from the
PE entry point down to the localized-string setup functions, plus how the
recompiled port in this folder relates to it.

The functions in question populate the per-LCID localized-string tables that
`GetLocaleEntry` / `GetLocalizedString` (in `common/common.cpp`) read. They are
the reason the "Wrong executable" error box has real text in the original.

---

## TL;DR

These functions have **no direct `call`** to them. They are **C-style static
global constructors** — entries in the `.ctors` function-pointer array at the
top of `.rdata` — invoked **indirectly** by the CRT's `__initterm` while the
process is starting up, **before `main`/`WinMain` runs**. That is why searching
for callers with normal cross-references finds nothing.

```
start -> ___tmainCRTStartup -> __cinit -> __initterm -> sub_1A5B5F0 / sub_1A5BC00 / ...
```

---

## The direct call path (original Fable3.exe)

```
start                            0x01523B39   <- PE entry point
  |  jmp
  v
___tmainCRTStartup               0x015239BB   MSVC CRT startup
  |  call __cinit                 (call site 0x01523A9D)
  |  ... (later calls WinMain at 0x005CF170)
  v
__cinit                          0x0151F94A   C/C++ static-init thunk
  |  call __initterm_e            (C++ ctors, 0x0151F980)
  |  mov eax, offset unk_1AD576C  (ctor array start, 0x0151F995)
  |  mov [arg1], offset First      (ctor array end  = 0x1ADDE88, 0x0151F99A)
  |  call __initterm              (0x0151F9A1)
  v
__initterm(0x1AD576C, 0x1ADDE88) 0x0151F891   walks the .ctors array
  |  for p in [0x1AD576C, 0x1ADDE88):
  |      if (*p != 0)  (*p)();     // skips null entries
  |
  |    (8647 slots; slot idx 0 is a null)
  +---> sub_1A5B5F0   0x01A5B5F0   [slot idx 7]  -> dword_1DBD010 = hash("GUI_WRONG_EXECUTABLE_NAME")
  +---> sub_1A5B620   0x01A5B620   [slot idx 8]  -> dword_1DBD014 = hash("GUI_WRONG_EXECUTABLE_DESC")
  |      ... ~8600 other static constructors ...
  +---> sub_1A5BC00   0x01A5BC00   [slot idx 53] -> fills the key table dword_1DBD2F4 (9 name hashes)
  +---> sub_1A5BDB0   0x01A5BDB0   [slot idx 54] -> fills the EN-US locale table 0x1DBD120
  +---> ... (rest of the array)
```

After `__initterm` returns, `___tmainCRTStartup` continues and eventually calls
`WinMain` (`0x005CF170`) -> the game's `main` -> `RunGame` -> `GameThread`,
where the "wrong executable" branch reads the now-populated values.

### `__initterm` body (0x0151F891) — the dispatcher

```asm
__initterm:                     ; eax = start (0x1AD576C), [ebp+First] = end (0x1ADDE88)
    mov  esi, eax
    jmp  short loc_151F8A6
loc_151F89B:
    mov  eax, [esi]             ; eax = current ctor pointer
    test eax, eax
    jz   short loc_151F8A3      ; skip nulls
    call eax                    ; <-- invoke the static constructor
loc_151F8A3:
    add  esi, 4
loc_151F8A6:
    cmp  esi, [ebp+First]
    jb   short loc_151F89B
    ret
```

Note the `test eax,eax / jz skip` — this build's `__initterm` **skips null
entries**, which is why the leading null slot (idx 0) is harmless.

### The `.ctors` array

- Location: `0x1AD576C .. 0x1ADDE88` (start of `.rdata`, right after `.idata`).
- Size: **8647 slots** (index 0 .. 8646); slot idx 0 = `0`, idx 8646 = `0x01A5AF10`
  (last real ctor), `0x1ADDE88` is the one-past-end sentinel passed as `First`.
- Each slot is a `void (*)(void)` function pointer.

---

## The four localized-string setup functions

### `sub_1A5B5F0` / `sub_1A5B620` — set the two "wrong executable" ID globals

Both compute a string-name hash and store it:

| Function | Global (VA) | String | Hash value |
|---|---|---|---|
| `sub_1A5B5F0` | `dword_1DBD010` | `GUI_WRONG_EXECUTABLE_NAME` | `0x9C839A2F` |
| `sub_1A5B620` | `dword_1DBD014` | `GUI_WRONG_EXECUTABLE_DESC`  | `0xDE401D85` |

### `sub_1A5BC00` — fills the 9-entry key table `dword_1DBD2F4`

| Index | VA | Name hash | Hash value |
|---|---|---|---|
| 0 | `0x1DBD2F4` | `GUI_OK` | `0xAA85B2AB` |
| 1 | `0x1DBD2F8` | `GUI_QUIT` | `0x42610528` |
| 2 | `0x1DBD2FC` | `GUI_SIGN_IN_REQUIRED_TITLE` | `0x5A6D19C7` |
| 3 | `0x1DBD300` | `GUI_SIGN_IN_REQUIRED` | `0x80140744` |
| 4 | `0x1DBD304` | `GUI_IMPROPER_SHUTDOWN_ERROR` | `0xEDE277AD` |
| 5 | `0x1DBD308` | `GUI_ERROR` | `0xCD7797A5` |
| 6 | `0x1DBD30C` | `GUI_DIRECTX_INITIALISE_ERROR` | `0x7665D03B` |
| 7 | `0x1DBD310` | `GUI_WRONG_EXECUTABLE_NAME` | `0x9C839A2F` |
| 8 | `0x1DBD314` | `GUI_WRONG_EXECUTABLE_DESC` | `0xDE401D85` |

### `sub_1A5BDB0` — fills the EN-US locale table `0x1DBD120` (9 entries)

| Index | VA | Wide string |
|---|---|---|
| 0 | `0x1DBD120` | `OK` |
| 1 | `0x1DBD124` | `Quit` |
| 2 | `0x1DBD128` | `Sign in required` |
| 3 | `0x1DBD12C` | `To play this game, you must be signed in to Games for Windows – LIVE. Do you want to sign in?` |
| 4 | `0x1DBD130` | `Improper shutdown detected - if this persists, please check the readme file.` |
| 5 | `0x1DBD134` | `Error` |
| 6 | `0x1DBD138` | `Direct3D failed to initialise. Please check your system settings and video drivers.` |
| 7 | `0x1DBD13C` | `Wrong executable` |
| 8 | `0x1DBD140` | `You cannot run the game directly from Fable3.exe. Please use FableLauncher.exe in the install directory to launch the game.` |

Each entry is written via the wide shared-slot ctor `sub_1A00310(slot, L"...", -1)`.

---

## The string-name hash algorithm

Used by all of the above (`sub_1A5B5F0`, `sub_1A5B620`, `sub_1A5BC00`):

```c
unsigned hash(const char *s) {
    unsigned v0 = 0x811C9DC5u;          // seed = -2128831035
    for (; *s; ++s)
        v0 = (unsigned char)*s ^ (0x01000193u * v0);   // prime = 16777619
    return v0;                          // mod 2^32 throughout
}
```

- Seed: `0x811C9DC5` (`-2128831035`)
- Prime: `0x01000193` (`16777619`)
- The trailing NUL is not folded in (it only terminates the loop).

---

## Where the values are consumed — the "Wrong executable" path

In `GameThread` (`engine/gamethread.cpp`, original `0x006156EB`/`0x00615705`),
when the shared-memory blob `-=[SMS_Fable3.exe_SMS]=-` was **not** found
(`g_byte_1DBD008 == 0`), the original shows this message box and exits:

```cpp
const WCHAR *title = GetLocalizedString((const WCHAR ***)GetLocaleEntry(dword_1DBD010));
const WCHAR *body  = GetLocalizedString((const WCHAR ***)GetLocaleEntry(dword_1DBD014));
MessageBoxW(hwnd, body, title, MB_ICONERROR);
ExitProcess(1);
```

- `dword_1DBD010 = 0x9C839A2F` -> key-table index 7 -> EN-US[7] = **"Wrong executable"**
- `dword_1DBD014 = 0xDE401D85` -> key-table index 8 -> EN-US[8] = **"You cannot run the
  game directly from Fable3.exe. Please use FableLauncher.exe in the install
  directory to launch the game."**

Because the setup functions run in `__initterm` at process start, these tables
are already populated by the time this branch can execute.

---

## Does the recompiled port (this folder) skip the start?

**Short answer: no — it does not skip `start`. It skips the *work* those static
constructors do.**

- The build (`build_vs2008.bat`) is a stock MSVC GUI app
  (`/subsystem:windows`, static CRT `/MT`, no custom `/ENTRY`). So the CRT
  startup chain `start -> ___tmainCRTStartup -> __cinit -> __initterm -> WinMain`
  is present and runs exactly as in the original. `WinMain` (`main.cpp`) is
  entered by the CRT, not jumped to manually.
- `__initterm` still runs, but it walks **this port's own** (much smaller)
  `.ctors` array, which contains **none** of the localization setup functions.
  There is no ported `sub_1A5BC00` / `sub_1A5BDB0` / `sub_1A5B5F0` / `sub_1A5B620`,
  and nothing in the sources writes to `g_localeKeys`, the `g_localeTable_*`
  arrays, `g_localeDefault`, or sets `g_dword_1DBD010`/`g_dword_1DBD014` to the
  real hashes.
- Result: after startup the locale tables are still their `0xFFFFFFFF`
  initializer and `g_dword_1DBD010`/`014` are still `0`. When the "wrong
  executable" branch runs, `GetLocaleEntry(0)` -> `&g_localeDefault[0]`
  (`0xFFFFFFFF`) -> `GetLocalizedString` reads `*obj = 0xFFFFFFFF`, passes the
  `!= 0` check, and dereferences `0xFFFFFFFF` -> `W32/0xC0000005` access
  violation.

So the port keeps the `start`/`__initterm` **mechanism** but omits the
**contents** of the original's 8647-entry static-ctor array (specifically the
four locale setup functions), which is the root cause of the crash.

### Fix options

1. **Faithful:** add the four functions as static constructors (or call them
   from `FrameworkInit`). Populate `g_localeKeys` (9 hashes), the EN-US
   `g_localeTable_1DBD120` (9 wide strings), and set
   `g_dword_1DBD010 = 0x9C839A2F`, `g_dword_1DBD014 = 0xDE401D85`. The box then
   shows the real "Wrong executable" text.
2. **Minimal un-crash:** on that error path, use the two literal English strings
   above instead of going through the (empty) locale tables.
3. **Safety net for every lookup:** in `GetLocalizedString`, also treat the
   `0xFFFFFFFF` sentinel as "no string":

   ```cpp
   const WCHAR **p = *obj;
   if (p != 0 && p != (const WCHAR **)(uintptr_t)0xFFFFFFFF)
       return *p;
   return g_defaultLocalizedString;
   ```

Note: even with the crash fixed, this branch still `ExitProcess(1)`s. To actually
*run* the game, the launcher (`FableLauncher.exe`) must create the
`-=[SMS_Fable3.exe_SMS]=-` shared-memory section (0x6C4 bytes) first; the blob
— not the message box — is what lets the game start.
