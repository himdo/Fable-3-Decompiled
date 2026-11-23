// ============================================================
//  Fable 3 -- the SubNode red-black tree (engine/subnode.cpp).
//
//  The game keeps several collections in red-black trees with
//  44-byte nodes (a 12-byte RB header + 36-byte payload + color /
//  sentinel-flag bytes, a 32-byte tree object with a heap-allocated
//  head node, a 44-byte heap sentinel node and a count).  This
//  module re-implements the family's range erase
//  (SubNodeEraseRange, the 0x626770 range erase) and the
//  single-node erase + rotation / iterator / free-subtree helpers
//  it depends on.  Only the range erase is public; the helpers
//  are static inside subnode.cpp.
// ============================================================
#ifndef F3_SUBNODE_H
#define F3_SUBNODE_H

// sub_626770 (0x626770 - 0x626853) -- the SubNode tree range erase (the
// MSVC debug std::map::erase(first, last) / _Erase range).  thiscall
// + `retn 0x14` = 5 stack args (out, bound0, start, bound1, end),
// declared object-first.  Erases every node from `start` through `end`
// (inclusive), decrementing the count each time, and writes the end
// iterator `{head, end}` into the 2-dword `out` (returning `out`).
// `bound0` / `bound1` are the original iterator tag dwords (each must
// equal the tree head, or `__invalid_parameter_noinfo` fires).  Fast
// path: `start` at the sentinel's leftmost and `end` at the sentinel
// frees the whole root subtree via SubNodeFreeSubtree and self-loops
// the sentinel (used to clear the tree to empty); otherwise it walks
// the in-order range via SubNodeIterNext and erases node-by-node
// through SubNodeEraseNode.  Replaces the no-op sub_626770 stub in
// gamemain.cpp (called from DestroyBankContainer 0x626EF0 for the
// container's +0x74 tree).
void *SubNodeEraseRange(void *obj, void *out, void *bound0, void *start,
                        void *bound1, void *end);

#endif  // F3_SUBNODE_H
