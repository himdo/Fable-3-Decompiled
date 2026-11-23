// Fable 3 -- asset-bank red-black tree (engine/banktree.cpp).
//
// The game stores its mounted asset banks in a red-black tree (the MSVC
// _Rb_tree layout: a 24-byte {left, parent, right, key, object, color,
// sentinel-flag} node header, a sentinel head node, and a node count).
// This module re-implements the tree's single-node erase and the
// range-erase built on top of it, plus the small rotation / iterator /
// free helpers they depend on.  The public entry point is
// BankTreeEraseRange (the 0x624560 range-erase).
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

#endif  // F3_BANKTREE_H
