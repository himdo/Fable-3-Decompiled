// Fable 3 -- asset-bank red-black tree implementation (engine/banktree.cpp).
//
// Re-implements the game's bank-tree red-black-tree node erase (sub_623630)
// and the range-erase built on top of it (sub_624560), plus the rotation,
// leftmost/rightmost, in-order iterator-advance, and subtree-free helpers
// they call.  Every function carries its original IDA address range and a
// `[NOT byte-identical]` note (each has E8 relative calls and/or high-VA
// references, so a naked-asm byte match is not possible).
//
// Bank-tree node layout (24-byte RB header + payload):
//   +0x00  left   (void*)
//   +0x04  parent (void*)
//   +0x08  right  (void*)
//   +0x0C  key
//   +0x10  object
//   +0x14  color  (BYTE: 0 = red, 1 = black)
//   +0x15  sentinel flag (BYTE: non-zero marks the head/sentinel node)
//
// Bank-tree object layout (24 bytes):
//   +0x00  head     (a node pointer, carried into the out-iterator)
//   +0x18  sentinel (the RB head node: left = leftmost, parent = root,
//                    right = rightmost, flag byte +0x15 set)
//   +0x1C  count    (node count, unsigned)

#include <stdexcept>

#include "banktree.h"
#include "common/common.h"   // LHFree
#include "xlip.h"            // F3InvalidParam

// The original's invalid-iterator guard throws a std::out_of_range
// ("invalid map/set<T> iterator") after routing the exception through the
// CRT's source-string builder and the global STL report handler
// (dword_1DDCFC8).  That path is only reached on a sentinel node, which the
// game never passes here, so a plain std::out_of_range throw is
// functionally equivalent for the reachable code path.
static __declspec(noreturn) void BankTreeThrowOutOfRange(void)
{
    throw std::out_of_range("invalid map/set<T> iterator");
}

// ============================================================
//  BankTreeLeftmost  (0x61E950 - 0x61E96A)  -- leftmost node of the subtree
//  rooted at `node`
//  [NOT byte-identical] placement-dependent (the relative recursive-style
//  self-branch; no other references, but the loop is not a single jump).
//
//  void *BankTreeLeftmost(void *node)   -- cdecl, one arg, returns the node
//
//  Walks the left child (+0x00) links, stopping at the last non-sentinel
//  node (the sentinel has its flag byte at +0x15 set).
// ============================================================
static void *BankTreeLeftmost(void *node)
{
    char *cur = (char *)node;
    for (;;)
    {
        char *left = (char *)*(void **)cur;
        if (*(BYTE *)(left + 0x15) != 0)      // the sentinel
            break;
        cur = left;
    }
    return cur;
}

// ============================================================
//  BankTreeRightmost  (0x61E930 - 0x61E94B)  -- rightmost node of the
//  subtree rooted at `node`
//  [NOT byte-identical] placement-dependent (the relative self-branch).
//
//  void *BankTreeRightmost(void *node)   -- cdecl, one arg
//
//  Walks the right child (+0x08) links, stopping at the last non-sentinel
//  node.
// ============================================================
static void *BankTreeRightmost(void *node)
{
    char *cur = (char *)node;
    for (;;)
    {
        char *right = (char *)*(void **)(cur + 8);
        if (*(BYTE *)(right + 0x15) != 0)      // the sentinel
            break;
        cur = right;
    }
    return cur;
}

// ============================================================
//  BankTreeRotateLeft  (0x620910 - 0x62095B)  -- red-black tree rotate-left
//  [NOT byte-identical] placement-dependent (refs the tree's sentinel at
//  obj+0x18 for the root update).
//
//  void BankTreeRotateLeft(void *obj, void *x)   -- thiscall + retn 4
//
//  Standard MSVC _Rb_tree rotate-left of node `x` (y = x->right): y takes x's
//  place, x becomes y's left child.
// ============================================================
static void BankTreeRotateLeft(void *obj, void *x)
{
    char *X = (char *)x;
    char *Y = (char *)*(void **)(X + 8);          // y = x->right
    char *yl = (char *)*(void **)Y;               // y->left

    *(void **)(X + 8) = yl;                        // x->right = y->left
    if (*(BYTE *)(yl + 0x15) == 0)
        *(void **)(yl + 4) = X;                    // y->left->parent = x

    char *xp = (char *)*(void **)(X + 4);          // x->parent
    *(void **)(Y + 4) = xp;                        // y->parent = x->parent

    char *sent = (char *)*(void **)((char *)obj + 0x18);
    if (X == *(void **)(sent + 4)) {               // x was the root
        *(void **)(sent + 4) = Y;                  // root = y
        *(void **)Y = X;                           // y->left = x
        *(void **)(X + 4) = Y;                     // x->parent = y
    } else if (X == *(void **)(xp)) {              // x was the left child
        *(void **)(xp) = Y;                        // parent->left = y
        *(void **)Y = X;                           // y->left = x
        *(void **)(X + 4) = Y;                     // x->parent = y
    } else {                                       // x was the right child
        *(void **)(xp + 8) = Y;                    // parent->right = y
        *(void **)Y = X;                           // y->left = x
        *(void **)(X + 4) = Y;                     // x->parent = y
    }
}

// ============================================================
//  BankTreeRotateRight  (0x61E990 - 0x61E9DF)  -- red-black tree rotate-right
//  [NOT byte-identical] placement-dependent (refs the tree's sentinel at
//  obj+0x18 for the root update).
//
//  void BankTreeRotateRight(void *obj, void *x)   -- thiscall + retn 4
//
//  Mirror of BankTreeRotateLeft (y = x->left).
// ============================================================
static void BankTreeRotateRight(void *obj, void *x)
{
    char *X = (char *)x;
    char *Y = (char *)*(void **)X;                // y = x->left
    char *yr = (char *)*(void **)(Y + 8);         // y->right

    *(void **)X = yr;                             // x->left = y->right
    if (*(BYTE *)(yr + 0x15) == 0)
        *(void **)(yr + 4) = X;                   // y->right->parent = x

    char *xp = (char *)*(void **)(X + 4);         // x->parent
    *(void **)(Y + 4) = xp;                       // y->parent = x->parent

    char *sent = (char *)*(void **)((char *)obj + 0x18);
    if (X == *(void **)(sent + 4)) {              // x was the root
        *(void **)(sent + 4) = Y;                 // root = y
        *(void **)(Y + 8) = X;                    // y->right = x
        *(void **)(X + 4) = Y;                    // x->parent = y
    } else if (X == *(void **)(xp + 8)) {         // x was the right child
        *(void **)(xp + 8) = Y;                   // parent->right = y
        *(void **)(Y + 8) = X;                    // y->right = x
        *(void **)(X + 4) = Y;                    // x->parent = y
    } else {                                      // x was the left child
        *(void **)(xp) = Y;                       // parent->left = y
        *(void **)(Y + 8) = X;                    // y->right = x
        *(void **)(X + 4) = Y;                    // x->parent = y
    }
}

// ============================================================
//  BankTreeIterNext  (0x61EC30 - 0x61EC92)  -- advance a red-black tree
//  in-order iterator to its successor (in place)
//  [NOT byte-identical] placement-dependent (the __invalid_parameter_noinfo
//  IAT-slot calls).
//
//  void BankTreeIterNext(void *iter)   -- thiscall, plain retn
//
//  `iter` is an adjacent {tag, node} pair (the original passes the address of
//  the caller's two stack slots).  tag (iter+0) must be non-zero; node
//  (iter+4) must not be the sentinel.  When node has a real right child the
//  successor is the leftmost node of that right subtree; otherwise it walks
//  the parent chain (+0x04) up until the first ancestor that is not a right
//  child (or the sentinel).
// ============================================================
static void BankTreeIterNext(void *iter)
{
    if (*(void **)iter == 0)
        F3InvalidParam();
    char *np = (char *)iter + 4;                 // &node
    void *node = *(void **)np;
    if (*(BYTE *)((char *)node + 0x15) != 0) {   // node is the sentinel
        F3InvalidParam();
        return;                                   // original: jmp (no return)
    }

    void *right = *(void **)((char *)node + 8);
    if (*(BYTE *)((char *)right + 0x15) == 0) {  // a real right child
        void *cur = right;
        void *nxt = *(void **)cur;
        while (*(BYTE *)((char *)nxt + 0x15) == 0) {
            cur = nxt;
            nxt = *(void **)cur;
        }
        *(void **)np = cur;                        // successor = leftmost(right)
    } else {                                       // the sentinel right child
        void *anc = *(void **)((char *)node + 4);
        for (;;) {
            if (*(BYTE *)((char *)anc + 0x15) != 0)
                break;                             // ancestor is the sentinel
            if (*(void **)np != *(void **)((char *)anc + 8))
                break;                             // node is not the right child
            *(void **)np = anc;
            anc = *(void **)((char *)anc + 4);
        }
        *(void **)np = anc;
    }
}

// ============================================================
//  BankTreeFreeSubtree  (0x621630 - 0x621662)  -- free every node of the
//  red-black subtree rooted at `node`
//  [NOT byte-identical] placement-dependent (the recursive call) and refs
//  the heap table (dword_2075B24) at its original high VA (the node frees go
//  through it, i.e. LHFree).
//
//  void BankTreeFreeSubtree(void *obj, void *node)   -- thiscall + retn 4
//
//  Frees the subtree rooted at `node`: recurse into the node's right subtree
//  (+0x08), free the node through the heap, then advance to the node's left
//  child (+0x00) and repeat.  Stops (without freeing) at the sentinel -- the
//  caller frees the sentinel node separately.
// ============================================================
static void BankTreeFreeSubtree(void *obj, void *node)
{
    for (;;)
    {
        char *n = (char *)node;
        if (*(BYTE *)(n + 0x15) != 0)             // the sentinel
            break;
        BankTreeFreeSubtree(obj, *(void **)(n + 8));   // the right subtree
        LHFree(node);                              // free this node
        node = *(void **)n;                        // advance to the left child
    }
}

// ============================================================
//  BankTreeEraseNode  (0x623630 - 0x623953)  -- erase one red-black tree node
//  [NOT byte-identical] placement-dependent (the relative calls to the
//  iterator-advance / leftmost / rightmost / rotate helpers and LHFree) and
//  refs the high-VA exception machinery (dword_1DDCFC8, the std::out_of_range
//  / logic_error vtables, sub_402F00 / sub_401BD0 / std::_Throw) -- the
//  invalid-iterator throw is simplified to a std::out_of_range.
//
//  void *BankTreeEraseNode(void *obj, void *out, void *tag, void *node)
//
//  Original: thiscall + `retn 0xC` (obj in ECX, the 8-byte {tag, node}
//  out-iterator scratch in EDX, the erase `tag`/`node` pair on the stack).
//  `node` must not be the sentinel (std::out_of_range otherwise).  It
//  advances the {tag, node} iterator to the in-order successor, unlinks the
//  node from the tree (relinking the successor into its place when it has two
//  children), rebalances when the removed node was black, frees the node,
//  decrements the tree's count (+0x1C), and writes {head, successor} to the
//  out-iterator (returned).
// ============================================================
static void *BankTreeEraseNode(void *obj, void *out, void *tag, void *node)
{
    char *m    = (char *)obj;
    char *sent = (char *)*(void **)(m + 0x18);    // the sentinel head node

    if (*(BYTE *)((char *)node + 0x15) != 0)
        BankTreeThrowOutOfRange();               // throws; does not return

    char *N = (char *)node;                       // the original node (ebp)

    // Advance the {tag, node} iterator to the in-order successor.  The
    // original keeps the pair on the caller's stack (arg_4 / arg_8, 4 bytes
    // apart) and advances arg_8 in place; here it is staged in an adjacent
    // 2-dword local so the successor lands in `node` (used for out[1]).
    {
        void *it[2];
        it[0] = tag;
        it[1] = node;
        BankTreeIterNext(it);
        node = it[1];                              // node <- successor
    }

    // Register-style locals mirroring the original's working registers.
    char *eax = 0, *ecx = 0, *edx = 0, *esi = 0, *edi = 0;
    unsigned char dl = 0;
    char *left = 0, *right = 0;

    // ---- pick the node `edi` to splice in / relink, and relink the tree ----
    ecx = (char *)*(void **)N;                    // N->left
    if (*(BYTE *)(ecx + 0x15) != 0) {             // N->left is the sentinel
        edi = (char *)*(void **)(N + 8);          // edi = N->right
        goto splice;
    }
    edx = (char *)*(void **)(N + 8);              // N->right
    if (*(BYTE *)(edx + 0x15) != 0) {             // N->right is the sentinel
        edi = ecx;                                // edi = N->left
        goto splice;
    }
    // N has both children: the successor `S` moves into N's position.
    eax = (char *)node;                           // S (the advanced node)
    edi = (char *)*(void **)(eax + 8);            // S->right
    if (eax == N)                                 // degenerate (S == N)
        goto splice;
    goto relink_s;

splice:
    esi = (char *)*(void **)(N + 4);              // esi = N->parent
    if (*(BYTE *)(edi + 0x15) == 0)               // splice in a real child
        *(void **)(edi + 4) = esi;                // (edi)->parent = N->parent

    // relink `edi` into N's position
    if (*(void **)(sent + 4) == N)                // N was the root
        *(void **)(sent + 4) = edi;
    else if (*(void **)(esi) == N)                // N was the left child
        *(void **)(esi) = edi;
    else                                          // N was the right child
        *(void **)(esi + 8) = edi;

    // update the sentinel's leftmost / rightmost pointers
    if (*(void **)(sent) == N) {
        if (*(BYTE *)(edi + 0x15) != 0)
            *(void **)(sent) = esi;
        else
            *(void **)(sent) = BankTreeLeftmost(edi);
    }
    if (*(void **)(sent + 8) == N) {
        if (*(BYTE *)(edi + 0x15) != 0)
            *(void **)(sent + 8) = esi;
        else
            *(void **)(sent + 8) = BankTreeRightmost(edi);
    }
    goto rebalance;

relink_s:
    // S (=eax) takes N's spot; S's old position is spliced in S->right.
    *(void **)((char *)ecx + 4) = eax;            // (N->left)->parent = S
    ecx = (char *)*(void **)N;                    // N->left
    *(void **)eax = ecx;                          // S->left = N->left
    if (eax != *(void **)(N + 8)) {               // S is not directly N->right
        esi = (char *)*(void **)(eax + 4);        // esi = S->parent
        if (*(BYTE *)(edi + 0x15) == 0)
            *(void **)(edi + 4) = esi;            // (S->right)->parent
        *(void **)(esi) = edi;                    // (S->parent)->left = S->right
        *(void **)(eax + 8) = *(void **)(N + 8);  // S->right = N->right
        *(void **)((char *)*(void **)(N + 8) + 4) = eax;  // (N->right)->parent = S
        // esi stays = S->parent (the node above the erased node)
    } else {
        esi = eax;                                 // esi = S (S is N->right)
    }

    // relink S (in N's spot) against N's parent
    {
        char *Nparent = (char *)*(void **)(N + 4);
        if (*(void **)(sent + 4) == N)             // N was the root
            *(void **)(sent + 4) = eax;
        else if (*(void **)(Nparent) == N)
            *(void **)(Nparent) = eax;
        else
            *(void **)(Nparent + 8) = eax;
        *(void **)(eax + 4) = Nparent;             // S->parent = N->parent
    }

    // S takes N's color (N is freed below)
    {
        unsigned char c = *(BYTE *)(N + 0x14);
        *(BYTE *)(N + 0x14) = *(BYTE *)(eax + 0x14);
        *(BYTE *)(eax + 0x14) = c;
    }
    goto rebalance;

rebalance:
    if (*(BYTE *)(N + 0x14) != 1)                 // N was red -> no fix-up
        goto free_node;
    if (edi == *(void **)(sent + 4))              // edi is the root
        goto rebalance_done;
    for (;;) {
        if (*(BYTE *)(edi + 0x14) != 1)           // edi is red
            goto rebalance_done;

        // ---- the sibling is `eax` (left or right of `esi` = N->parent) ----
        eax = (char *)*(void **)esi;               // esi->left
        if (edi != eax)
            goto edi_right;

        // edi == esi->left: the sibling is esi->right
        eax = (char *)*(void **)(esi + 8);
        if (*(BYTE *)(eax + 0x14) == 0) {         // sibling is red
            *(BYTE *)(eax + 0x14) = 1;
            *(BYTE *)(esi + 0x14) = 0;
            BankTreeRotateLeft(obj, esi);
            eax = (char *)*(void **)(esi + 8);
        }
        if (*(BYTE *)(eax + 0x15) != 0)           // sibling is the sentinel
            goto advance;
        left = (char *)*(void **)eax;
        right = (char *)*(void **)(eax + 8);
        if (*(BYTE *)(left + 0x14) == 1 && *(BYTE *)(right + 0x14) == 1) {
            *(BYTE *)(eax + 0x14) = 0;            // both children black
            goto advance;
        }
        if (*(BYTE *)(left + 0x14) != 1) {        // left red (right black)
            *(BYTE *)(left + 0x14) = 1;
            *(BYTE *)(eax + 0x14) = 0;
            BankTreeRotateRight(obj, eax);
            eax = (char *)*(void **)(esi + 8);
        }
        // right is red: recolor and rotate-left, then done
        dl = *(BYTE *)(esi + 0x14);
        *(BYTE *)(eax + 0x14) = dl;
        *(BYTE *)(esi + 0x14) = 1;
        eax = (char *)*(void **)(eax + 8);
        *(BYTE *)(eax + 0x14) = 1;
        BankTreeRotateLeft(obj, esi);
        goto rebalance_done;

edi_right:
        // edi == esi->right: the sibling is esi->left
        if (*(BYTE *)(eax + 0x14) == 0) {         // sibling is red
            *(BYTE *)(eax + 0x14) = 1;
            *(BYTE *)(esi + 0x14) = 0;
            BankTreeRotateRight(obj, esi);
            eax = (char *)*(void **)esi;
        }
        if (*(BYTE *)(eax + 0x15) != 0)           // sibling is the sentinel
            goto advance;
        left = (char *)*(void **)eax;
        right = (char *)*(void **)(eax + 8);
        if (*(BYTE *)(left + 0x14) == 1 && *(BYTE *)(right + 0x14) == 1) {
            *(BYTE *)(eax + 0x14) = 0;            // both children black
            goto advance;
        }
        if (*(BYTE *)(left + 0x14) == 1) {        // left black (right red)
            *(BYTE *)(right + 0x14) = 1;
            *(BYTE *)(eax + 0x14) = 0;
            BankTreeRotateLeft(obj, eax);
            eax = (char *)*(void **)esi;
        }
        // left is red: recolor and rotate-right, then done
        dl = *(BYTE *)(esi + 0x14);
        *(BYTE *)(eax + 0x14) = dl;
        *(BYTE *)(esi + 0x14) = 1;
        eax = (char *)*(void **)eax;
        *(BYTE *)(eax + 0x14) = 1;
        BankTreeRotateRight(obj, esi);
        goto rebalance_done;

advance:
        edi = esi;                                 // climb to the parent
        esi = (char *)*(void **)(esi + 4);
        if (edi == *(void **)(sent + 4))           // reached the root
            goto rebalance_done;
        continue;                                  // re-test at the new level

rebalance_done:
        *(BYTE *)(edi + 0x14) = 1;                 // mark the node black
    }

free_node:
    LHFree(N);                                     // free the erased node
    {
        unsigned int count = *(DWORD *)(m + 0x1C);
        if (count > 0)
            *(DWORD *)(m + 0x1C) = count - 1;
    }
    *(void **)((char *)out + 4) = node;            // out[1] = successor
    *(void **)(out) = *(void **)(m);               // out[0] = head
    return out;
}

// ============================================================
//  BankTreeEraseRange  (0x624560 - 0x624643)  -- the target: erase the bank
//  tree's in-order node range [start, end)
//  [NOT byte-identical] placement-dependent (the relative calls to the
//  subtree-free / iterator-advance / node-erase helpers and the
//  __invalid_parameter_noinfo IAT slot).
//
//  void *BankTreeEraseRange(void *obj, void *out, void *bound0, void *start,
//                           void *bound1, void *end)
//
//  Original: thiscall + `retn 0x14` (obj in ECX, five stack args).  The
//  bounds must match the tree's head (obj+0x00): `bound0`/`bound1` are each
//  non-null and == the head, `start` (== bound0 in the fast path) is the
//  sentinel's leftmost node, and `end` (== the sentinel) is the erase limit.
//
//  Fast path (start == leftmost && end == sentinel): free the whole tree from
//  the root, reset the sentinel to an empty self-referencing node, clear the
//  count, and write {head, sentinel} to the out-iterator.
//
//  Otherwise (the generic path): loop -- advance the {start, node} iterator
//  to its successor and erase the (previous) node -- until the running node
//  reaches `end`, then write {head, node} to the out-iterator.  Returns the
//  out-iterator (the caller discards the pointer).
// ============================================================
void *BankTreeEraseRange(void *obj, void *out, void *bound0, void *start,
                         void *bound1, void *end)
{
    char *m       = (char *)obj;
    char *sent    = (char *)*(void **)(m + 0x18);  // the sentinel head node
    void *head    = *(void **)(m);                  // obj+0x00
    void *leftmost = *(void **)(sent);              // sentinel->left
    char *v14[2];                                   // 8-byte out-iterator scratch

    // bound0 must be non-null and == the head
    if (start == 0 || start != head)
        F3InvalidParam();
    if (bound0 != leftmost)
        goto generic;
    // bound1 must be non-null and == the head
    if (bound1 == 0 || bound1 != head)
        F3InvalidParam();
    if (end != sent)
        goto generic;

    // ---- fast path: clear the whole tree, return {head, sentinel} ----
    BankTreeFreeSubtree(obj, *(void **)(sent + 4));  // free the root subtree
    *(void **)(sent + 4) = sent;                     // sentinel->parent = sentinel
    *(DWORD *)(m + 0x1C) = 0;                        // count = 0
    *(void **)sent = sent;                           // sentinel->left = sentinel
    *(void **)(sent + 8) = sent;                     // sentinel->right = sentinel
    *(void **)out = 0;
    *(void **)((char *)out + 4) = *(void **)sent;
    *(void **)out = head;
    return out;

generic:
    for (;;) {
        if (bound0 == 0 || bound0 != bound1)
            F3InvalidParam();
        if (start == end)
            break;
        void *oldStart = start;                        // the node to erase
        {
            // The iterator is the adjacent {bound0, start} pair; advancing it
            // moves `start` to its in-order successor.
            void *it[2];
            it[0] = bound0;
            it[1] = start;
            BankTreeIterNext(it);
            start = it[1];
        }
        BankTreeEraseNode(obj, v14, bound0, oldStart);  // erase the old node
    }
    *(void **)out = head;                              // out[0] = head
    *(void **)((char *)out + 4) = start;               // out[1] = node
    return out;
}
