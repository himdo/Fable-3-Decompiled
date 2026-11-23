// ============================================================
//  Fable 3 -- the bank name-table red-black tree (engine/namemap.cpp).
//
//  The game keeps the bank container's u32 -> wide-string name
//  entries in a red-black tree (the MSVC _Rb_tree debug layout:
//  a 48-byte node with a 24-byte RB header + key + wide-string
//  payload, a 24-byte tree object with a 4-byte head word, a
//  48-byte sentinel node and a count).  This module re-implements
//  the tree's range erase (NameMapEraseRange, the 0x5AB5F0 range
//  erase) and the single-node erase + rotation / iterator /
//  free-subtree helpers it depends on.  Only the range erase is
//  public; the helpers are static inside namemap.cpp.
// ============================================================
#ifndef F3_NAMEMAP_H
#define F3_NAMEMAP_H

// sub_5AB5F0 (0x5AB5F0 - 0x5AB6BF) -- the name-map range erase (the
// MSVC debug std::map::erase(first, last) / _Erase range).  thiscall
// + `retn 0x14` = 5 stack args (bound0, out, start, bound1, end),
// declared object-first.  Erases every node from `start` through
// `end` (inclusive), decrementing the count each time, and writes
// the end iterator `{head, end}` into the 2-dword `out` (returning
// `out`).  `bound0` / `bound1` are the original iterator tag dwords
// (each must equal the tree head, or `__invalid_parameter_noinfo`
// fires).  Fast path: `start` at the sentinel's leftmost and `end`
// at the sentinel frees the whole root subtree via NameMapFreeSubtree
// and self-loops the sentinel (used to clear the tree to empty);
// otherwise it walks the in-order range via NameMapIterNext and
// erases node-by-node through NameMapEraseNode.  Replaces the
// no-op sub_5AB5F0 stub in gamemain.cpp (called from
// DestroyBankContainer 0x626EF0 for the container's +0x94 tree).
void *NameMapEraseRange(void *obj, void *out, void *bound0, void *start,
                        void *bound1, void *end);

#endif  // F3_NAMEMAP_H
