// Fable 3 -- asset-bank red-black tree (engine/banktree.cpp).
//
// The game stores its mounted asset banks in a red-black tree (the MSVC
// _Rb_tree layout: a 24-byte {left, parent, right, key, object, color,
// sentinel-flag} node header, a sentinel head node, and a node count).
// This module re-implements the tree's single-node erase and the
// range-erase built on top of it, plus the small rotation / iterator /
// free helpers they depend on.  The public entry points are
// BankTreeEraseRange (the 0x624560 range-erase) and InitBankTree
// (the 0x6527D0 tree-object init, the range-erase counterpart).
#ifndef F3_BANKTREE_H
#define F3_BANKTREE_H

// sub_624560 (0x624560 - 0x624643) -- erase the in-order node range from
// `node` up to (but not including) `end` from the bank tree, and write the
// {head, successor} out-iterator pair into `out` (returned).
//
// Original: thiscall + `retn 0x14` (obj in ECX, five stack args).  `head`
// (obj+0x00) and `sentinel` (obj+0x18) / `count` (obj+0x1C) make up the
// 24-byte tree object.  a3/a5 are the (head, head) debug-iterator bounds,
// a4/a6 are the (start node, end node) range bounds.
//
// void *BankTreeEraseRange(void *obj, void *out, void *bound0, void *start,
//                          void *bound1, void *end)
void *BankTreeEraseRange(void *obj, void *out, void *bound0, void *start,
                         void *bound1, void *end);

// sub_6527D0 (0x6527D0 - 0x652841) -- the bank-tree object init (the
// BankTreeEraseRange counterpart).  thiscall + `retn 8` (obj in ECX,
// two stack args unused), declared object-first.  Initializes the
// 24-byte tree object in place: +0x00 = the 4-byte self-referencing head
// node (the block stores the tree object pointer; NULL on allocation
// failure), +0x18 = the freshly allocated 24-byte self-looped sentinel
// node (left / parent / right all self-referencing, color +0x14 = black,
// sentinel flag +0x15 = 1, the three self-reference dwords self-pointing
// at the block -- the payload words left untouched, exactly as the
// original does -- and the node count zeroed in +0x1C).  Replaces the
// no-op sub_6527D0 stub in gamemain.cpp (called from InitBankContainer
// 0x7201B0 for the container's +0xCC tree).
void *InitBankTree(void *obj);

#endif  // F3_BANKTREE_H
