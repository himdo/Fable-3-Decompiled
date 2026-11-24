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
//  it depends on.  Only the range erase and the tree-object init are
//  public; the helpers are static inside subnode.cpp.
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

// sub_7281F0 (0x7281F0 - 0x728264) -- the SubNode tree object init (the
// +0x74 SubNode collection's counterpart to SubNodeEraseRange).  thiscall
// + `retn 8` (obj in ECX, two stack args unused), declared object-first.
// Initializes the 32-byte tree object in place: +0x00 = the 4-byte
// self-referencing head node (the block stores the tree object pointer),
// +0x18 = the freshly allocated 44-byte sentinel node (left / parent / right
// all self-referencing, color +0x28 = black, sentinel flag +0x29 = 1, the
// three self-reference dwords self-pointing at the block, the payload words
// left untouched -- the original does not zero them -- and the node count
// zeroed in +0x1C).  Replaces the no-op sub_7281F0 stub in gamemain.cpp
// (called from InitBankContainer 0x7201B0 for the container's +0x74 tree).
void *InitSubNodeTree(void *obj);

#endif  // F3_SUBNODE_H
