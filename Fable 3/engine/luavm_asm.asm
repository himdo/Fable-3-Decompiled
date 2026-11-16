; ============================================================
;  luavm_asm.asm  -- MASM routines for engine/luavm.cpp that
;  need byte-exact encodings the C inline assembler cannot
;  emit. Assembled by ml.exe (VS2008) in build_vs2008.bat and
;  test_engine_build.bat; the objects are named
;  luavm_asm.obj and linked in alongside luavm.obj.
; ============================================================
.686P
.model flat
.code

; ============================================================
;  KoreForEachAllocNode  (0x5DC610 - 0x5DC721)  -- walk every
;  alloc node registered in the VM's alloc-node registry (the
;  heap+0x50 sub-object) and invoke the callback (a1 = the
;  registry's +0x0C context pointer, a2 = the node) on each.
;  When the registry's +0x7E58 single node is non-null it is
;  the only visit and the callback's result is returned.
;  Otherwise, when the +0x7E40 record count is non-zero, the
;  row chain is walked: each row (the +0x7E38 head, linked
;  through +0x7B04) owns a block of three 0x1EC0-byte table
;  records at +0x7B00; each record's +0x04 node is visited,
;  and when that node is null or child-less (node+0x18 == 0)
;  and the record's +0xB20 flag is set, the record's inline
;  node array is visited too: the 12-byte chain elements at
;  +0xB18 (each owning a 28-byte run of 4-byte nodes at
;  +0x20, linked through +0x24) are walked up to the record's
;  +0xB24 end pointer, invoking the callback on every 4-byte
;  slot. The walk ends when the record pointer reaches the
;  registry's +0x7E44 end (or becomes null); the last row
;  pointer is returned. The callback is a plain cdecl
;  int(int, int).
;
;  Byte-identical: the original's exact instruction sequence
;  with the original thiscall + retn 4 convention (the
;  registry in ECX, the callback on the stack; the C side
;  declares `extern "C" int KoreForEachAllocNode(int,
;  int (*)(int, int))` and calls it through the
;  KoreForEachAllocFn __thiscall function-pointer cast). The
;  3-byte `lea ecx, [ecx]` nop after the entry jmp is emitted
;  as raw bytes (8D 49 00) because both the C inline
;  assembler and ml normalize the zero-displacement lea to
;  the 2-byte 8D 09 form; every other byte is the natural
;  encoding of the instruction sequence.
; ============================================================
_KoreForEachAllocNode PROC
    sub     esp, 10h
    push    ebp
    mov     ebp, ecx
    mov     eax, [ebp+7E58h]
    test    eax, eax
    jz      short f3_kfen_count
    push    eax
    mov     eax, [ebp+0Ch]
    push    eax
    call    dword ptr [esp+20h]
    add     esp, 8
    pop     ebp
    add     esp, 10h
    ret     4
f3_kfen_count:
    cmp     dword ptr [ebp+7E40h], 0
    jz      f3_kfen_exit_ebp
    mov     eax, [ebp+7E38h]
    push    ebx
    mov     ebx, [eax+7B00h]
    mov     [esp+0Ch], eax
    test    ebx, ebx
    jz      f3_kfen_exit_ebx
    push    esi
    push    edi
    jmp     short f3_kfen_loop
    db      8Dh, 49h, 00h           ; the 3-byte nop, as the original
f3_kfen_loop:
    mov     eax, [ebx+4]
    test    eax, eax
    jz      short f3_kfen_recheck
    mov     ecx, [ebp+0Ch]
    push    eax
    push    ecx
    call    dword ptr [esp+2Ch]
    add     esp, 8
f3_kfen_recheck:
    mov     eax, [ebx+4]
    test    eax, eax
    jz      short f3_kfen_chain
    cmp     dword ptr [eax+18h], 0
    jnz     short f3_kfen_advance
f3_kfen_chain:
    cmp     dword ptr [ebx+0B20h], 0
    jz      short f3_kfen_advance
    mov     edi, [ebx+0B18h]
    mov     esi, [edi+20h]
    test    esi, esi
    jz      short f3_kfen_advance
f3_kfen_slot:
    mov     edx, [esi]
    mov     eax, [ebp+0Ch]
    push    edx
    push    eax
    call    dword ptr [esp+2Ch]
    mov     ecx, [edi+20h]
    add     ecx, 1Ch
    add     esp, 8
    cmp     esi, ecx
    jnz     short f3_kfen_bump
    mov     edi, [edi+24h]
    test    edi, edi
    jz      short f3_kfen_zero
    mov     esi, [edi+20h]
    jmp     short f3_kfen_end
f3_kfen_zero:
    xor     esi, esi
    xor     edi, edi
    jmp     short f3_kfen_end
f3_kfen_bump:
    add     esi, 4
f3_kfen_end:
    cmp     esi, [ebx+0B24h]
    jz      short f3_kfen_advance
    test    esi, esi
    jnz     short f3_kfen_slot
f3_kfen_advance:
    mov     eax, [esp+14h]
    mov     edx, [eax+7B00h]
    add     edx, 5C40h
    cmp     ebx, edx
    jnz     short f3_kfen_step
    mov     eax, [eax+7B04h]
    xor     ebx, ebx
    cmp     eax, ebx
    jz      short f3_kfen_nullrow
    mov     ebx, [eax+7B00h]
    mov     [esp+14h], eax
    jmp     short f3_kfen_bound
f3_kfen_nullrow:
    mov     [esp+14h], ebx
    jmp     short f3_kfen_bound
f3_kfen_step:
    add     ebx, 1EC0h
f3_kfen_bound:
    cmp     ebx, [ebp+7E44h]
    jz      short f3_kfen_done
    test    ebx, ebx
    jnz     f3_kfen_loop
f3_kfen_done:
    pop     edi
    pop     esi
f3_kfen_exit_ebx:
    pop     ebx
f3_kfen_exit_ebp:
    pop     ebp
    add     esp, 10h
    ret     4
_KoreForEachAllocNode ENDP

END
