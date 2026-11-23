// Fable 3 -- bank name-table red-black tree (engine/namemap.cpp).
//
// Re-implements the u32 -> wide-string name-table red-black tree's
// range erase (sub_5AB5F0) and the single-node erase + rotation /
// leftmost / rightmost / iterator-advance / free-subtree helpers it
// calls.  Every function carries its original IDA address range and
// a [NOT byte-identical] note.
//
// Name-map node layout (the 24-byte RB header + 24-byte payload):
//   +0x00  left    (void *)
//   +0x04  parent  (void *)
//   +0x08  right   (void *)
//   +0x0C  key     (DWORD, compared unsigned)
//   +0x10  the 24-byte wide-string value:
//     +0x10    d0 (DWORD)
//     +0x14    buffer (SSO inline when cap < 8, heap pointer otherwise)
//     +0x24    len   (wide-char count)
//     +0x28    cap   (7 = the empty / reset sentinel, as in F3WString)
//   +0x2C  color         (BYTE: 0 = red, 1 = black)
//   +0x2D  sentinel flag (BYTE: nonzero marks the head / sentinel node)
//
// Name-map object layout (0x20 bytes):
//   +0x00  head     (a node pointer, carried into the out-iterator)
//   +0x18  sentinel (the RB head node: left = leftmost, parent = root,
//                   right = rightmost, sentinel flag +0x2D set)
//   +0x1C  count    (node count, unsigned)
//
// The in-tree wide-string payload is destroyed on node free: the heap
// buffer is freed via LHFree when cap >= 8, then cap = 7, len = 0,
// and the low word of the buffer field = 0 (the empty-string reset).

#include <stdexcept>

#include "namemap.h"
#include "common/common.h"   // LHFree
#include "xlip.h"            // F3InvalidParam

// The original's invalid-iterator guard throws std::out_of_range
// ("invalid map/set<T> iterator") after running the message through
// the CRT source-string builder and the global STL report handler
// (dword_1DDCFC8).  That path is only reached with a sentinel node,
// which the game never feeds here, so the plain std::out_of_range
// throw is functionally equivalent for the reachable code paths.
static __declspec(noreturn) void NameMapThrowOutOfRange(void)
{
    throw std::out_of_range("invalid map/set<T> iterator");
}

// sub_5ABA40 (0x5ABA40 - 0x5ABA5B) -- the sentinel-aware leftmost
// descent (cdecl, the node pointer on the stack).  Walks the left
// child pointer while the node is not the sentinel (flag byte
// +0x2D); returns the last non-sentinel node.
// [NOT byte-identical] placement-dependent (no E8 calls, but the
// function body cannot be inlined at the original address range).
static void *NameMapLeftmost(void *node)
{
    char *cur = (char *)node;
    for (;;)
    {
        char *left = (char *)*(void **)cur;
        if (*(BYTE *)(left + 0x2D) != 0)        // sentinel
            break;
        cur = left;
    }
    return cur;
}

// sub_5ABA20 (0x5ABA20 - 0x5ABA3C) -- the sentinel-aware rightmost
// descent (cdecl, the node pointer on the stack).  Walks the right
// child pointer (+8) while the node is not the sentinel (flag byte
// +0x2D); returns the last non-sentinel node.
// [NOT byte-identical] placement-dependent (no E8 calls, but the
// function body cannot be inlined at the original address range).
static void *NameMapRightmost(void *node)
{
    char *cur = (char *)node;
    for (;;)
    {
        char *right = (char *)*(void **)(cur + 8);
        if (*(BYTE *)(right + 0x2D) != 0)       // sentinel
            break;
        cur = right;
    }
    return cur;
}

// sub_5AC630 (0x5AC630 - 0x5AC67E) -- the MSVC debug _Rb_tree left
// rotation (thiscall + `retn 4`; the node pointer X on the stack).
// Y = X->right: X->right = Y->left (repointing Y->left's parent to X
// when it is a real node), Y->parent = X->parent; X becomes Y's left
// child.  The root case is detected through the sentinel's +4.
// [NOT byte-identical] placement-dependent (no E8 calls, but the
// function body cannot be inlined at the original address range).
static void NameMapRotateLeft(void *obj, void *x)
{
    char *m = (char *)obj;
    char *sent = (char *)*(void **)(m + 0x18);  // sentinel head node
    char *X = (char *)x;
    char *Y = (char *)*(void **)(X + 8);
    char *YL = (char *)*(void **)Y;
    *(void **)(X + 8) = YL;
    if (*(BYTE *)(YL + 0x2D) == 0)              // YL is a real node
        *(void **)(YL + 4) = X;
    char *Xp = (char *)*(void **)(X + 4);
    *(void **)(Y + 4) = Xp;
    if (X == *(void **)(sent + 4)) {            // X was the root
        *(void **)(sent + 4) = Y;
        *(void **)Y = X;
        *(void **)(X + 4) = Y;
        return;
    }
    if (X == *(void **)Xp) {                    // X was a left child
        *(void **)Xp = Y;
    } else {
        *(void **)(Xp + 8) = Y;
    }
    *(void **)Y = X;
    *(void **)(X + 4) = Y;
}

// sub_5AC680 (0x5AC680 - 0x5AC6D1) -- the MSVC debug _Rb_tree right
// rotation (thiscall + `retn 4`; the node pointer X on the stack).
// X = Y->left: Y->left = X->right (repointing X->right's parent to Y
// when it is a real node), X->parent = Y->parent; Y becomes X's right
// child.  The root case is detected through the sentinel's +4.
// [NOT byte-identical] placement-dependent (no E8 calls, but the
// function body cannot be inlined at the original address range).
static void NameMapRotateRight(void *obj, void *x)
{
    char *m = (char *)obj;
    char *sent = (char *)*(void **)(m + 0x18);  // sentinel head node
    char *X = (char *)x;
    char *Y = (char *)*(void **)X;
    char *YR = (char *)*(void **)(Y + 8);
    *(void **)X = YR;
    if (*(BYTE *)(YR + 0x2D) == 0)              // YR is a real node
        *(void **)(YR + 4) = X;
    char *Xp = (char *)*(void **)(X + 4);
    *(void **)(Y + 4) = Xp;
    if (X == *(void **)(sent + 4)) {            // X was the root
        *(void **)(sent + 4) = Y;
        *(void **)(Y + 8) = X;
        *(void **)(X + 4) = Y;
        return;
    }
    if (X == *(void **)Xp) {                    // X was a left child
        *(void **)Xp = Y;
    } else {
        *(void **)(Xp + 8) = Y;
    }
    *(void **)(Y + 8) = X;
    *(void **)(X + 4) = Y;
}

// sub_5AC850 (0x5AC850 - 0x5AC8B3) -- the MSVC debug _Rb_tree
// in-order successor for a `{tag, node}` iterator (thiscall; the
// iterator pointer on the stack... actually the thiscall arg in ECX
// only).  Advances the iterator's node word (+4) to its in-order
// successor: the leftmost of the right subtree when the right child
// is a real node, else walking up from the node while the current
// node is its ancestor's right child.  `__invalid_parameter_noinfo`
// fires on a null tag or a sentinel node.  [NOT byte-identical]
// placement-dependent + the E8 call to __invalid_parameter_noinfo.
static void NameMapIterNext(void *iter)
{
    char *it = (char *)iter;
    if (*(void **)it == 0)
        F3InvalidParam();
    char *np = it + 4;
    char *node = (char *)*(void **)np;
    if (*(BYTE *)(node + 0x2D) != 0)            // sentinel
    {
        F3InvalidParam();
        return;
    }
    char *right = (char *)*(void **)(node + 8);
    if (*(BYTE *)(right + 0x2D) == 0) {         // right subtree exists
        char *cur = right;
        for (;;)
        {
            char *left = (char *)*(void **)cur;
            if (*(BYTE *)(left + 0x2D) != 0)    // leftmost found
                break;
            cur = left;
        }
        *(void **)np = cur;
    } else {
        char *anc = (char *)*(void **)(node + 4);
        for (;;)
        {
            if (*(BYTE *)(anc + 0x2D) != 0)     // hit the sentinel
                break;
            if (*(void **)np != *(void **)(anc + 8))
                break;
            *(void **)np = anc;
            anc = (char *)*(void **)(anc + 4);
        }
        *(void **)np = anc;
    }
}

// sub_5AC5C0 (0x5AC5C0 - 0x5AC627) -- the recursive free-subtree
// (thiscall + `retn 4`; the node pointer on the stack).  Walks the
// right child: frees each non-sentinel node (destroying the in-tree
// wide-string payload first), then continues on the left child,
// until the sentinel.  [NOT byte-identical] placement-dependent +
// the E8 calls to itself, LHFree, and the global heap slot.
static void NameMapFreeSubtree(void *obj, void *node)
{
    for (;;)
    {
        char *n = (char *)node;
        if (*(BYTE *)(n + 0x2D) != 0)           // sentinel
            break;
        NameMapFreeSubtree(obj, *(void **)(n + 8));   // right subtree
        // Destroy the in-tree wide-string payload.
        if (*(DWORD *)(n + 0x28) >= 8u)         // heap buffer
            LHFree(*(void **)(n + 0x14));
        *(DWORD *)(n + 0x28) = 7;               // cap  = the reset sentinel
        *(DWORD *)(n + 0x24) = 0;               // len  = 0
        *(WORD *)(n + 0x14) = 0;                // buffer low word = 0
        LHFree(n);
        node = *(void **)n;                     // left child
    }
}

// sub_5AC210 (0x5AC210 - 0x5AC58F) -- the MSVC debug _Rb_tree single
// node erase (thiscall + `retn 0xC` = 3 stack args; the out iterator
// pointer, the iterator tag and the node pointer).  Advances the
// `{tag, node}` iterator to its in-order successor, then unlinks the
// node and performs the red-black rebalance.  When the node has no
// left child (N->left is the sentinel) or no right child, the child
// (or the sentinel) is spliced into the node's place and the
// sentinel's leftmost / rightmost words are updated; when the node
// has both children, its in-order successor `S` moves into the node's
// position and the successor's right child is spliced in.  The
// rebalance loop rotates while the splice point needs a black parent
// (the sibling-red and both-black sibling cases), then the node is
// freed (its in-tree wide-string payload destroyed first), the count
// decremented, and the out iterator set to `{head, successor}`.
// Returns the out iterator.  `__invalid_parameter_noinfo` fires on a
// sentinel tag / node; a sentinel node throws std::out_of_range.
// [NOT byte-identical] placement-dependent (many E8 calls: the
// iterator advance, the two rotations, the leftmost / rightmost
// descents, the throw helpers, LHFree, and the global heap slot).
static void *NameMapEraseNode(void *obj, void *out, void *tag, void *node)
{
    char *m    = (char *)obj;
    char *sent = (char *)*(void **)(m + 0x18);  // sentinel head node

    if (*(BYTE *)((char *)node + 0x2D) != 0)
        NameMapThrowOutOfRange();               // throws; does not return

    char *N = (char *)node;                     // the original node

    // Advance the {tag, node} iterator to its in-order successor.
    {
        void *it[2];
        it[0] = tag;
        it[1] = node;
        NameMapIterNext(it);
        node = it[1];                           // node <- successor
    }

    char *eax = 0, *ecx = 0, *edx = 0, *esi = 0, *edi = 0;
    unsigned char dl = 0;

    ecx = (char *)*(void **)N;                  // N->left
    if (*(BYTE *)(ecx + 0x2D) != 0) {           // N->left is the sentinel
        edi = (char *)*(void **)(N + 8);        // edi = N->right
        goto splice;
    }
    edx = (char *)*(void **)(N + 8);            // N->right
    if (*(BYTE *)(edx + 0x2D) != 0) {           // N->right is the sentinel
        edi = ecx;                              // edi = N->left
        goto splice;
    }
    // N has both children: the successor `S` moves into N's place.
    eax = (char *)node;                         // S (the advanced node)
    edi = (char *)*(void **)(eax + 8);          // edi = S->right
    if (eax == N)                               // degenerate: S == N
        goto splice;
    goto relink_s;

splice:
    esi = (char *)*(void **)(N + 4);            // esi = N->parent
    if (*(BYTE *)(edi + 0x2D) == 0)             // splicing in a real child
        *(void **)(edi + 4) = esi;

    if (*(void **)(sent + 4) == N)
        *(void **)(sent + 4) = edi;
    else if (*(void **)(esi) == N)
        *(void **)(esi) = edi;
    else
        *(void **)(esi + 8) = edi;

    if (*(void **)(sent) == N) {
        if (*(BYTE *)(edi + 0x2D) != 0)
            *(void **)(sent) = esi;
        else
            *(void **)(sent) = NameMapLeftmost(edi);
    }
    if (*(void **)(sent + 8) == N) {
        if (*(BYTE *)(edi + 0x2D) != 0)
            *(void **)(sent + 8) = esi;
        else
            *(void **)(sent + 8) = NameMapRightmost(edi);
    }
    goto rebalance;

relink_s:
    *(void **)((char *)ecx + 4) = eax;          // (N->left)->parent = S
    ecx = (char *)*(void **)N;
    *(void **)eax = ecx;                        // S->left = N->left
    if (eax != *(void **)(N + 8)) {             // S is not N->right
        esi = (char *)*(void **)(eax + 4);      // esi = S->parent
        if (*(BYTE *)(edi + 0x2D) == 0)
            *(void **)(edi + 4) = esi;          // (S->right)->parent
        *(void **)(esi) = edi;                  // (S->parent)->left = S->right
        *(void **)(eax + 8) = *(void **)(N + 8);
        *(void **)((char *)*(void **)(N + 8) + 4) = eax;
    } else {
        esi = eax;                              // esi = S
    }
    {
        char *Np = (char *)*(void **)(N + 4);   // N->parent
        if (*(void **)(sent + 4) == N)
            *(void **)(sent + 4) = eax;
        else if (*(void **)(Np) == N)
            *(void **)(Np) = eax;
        else
            *(void **)(Np + 8) = eax;
        *(void **)(eax + 4) = Np;
    }
    {
        unsigned char c = *(BYTE *)(N + 0x2C);
        *(BYTE *)(N + 0x2C) = *(BYTE *)(eax + 0x2C);
        *(BYTE *)(eax + 0x2C) = c;
    }
    goto rebalance;

rebalance:
    if (*(BYTE *)(N + 0x2C) != 1)               // N was red: no fixup
        goto free_node;
    if (edi == *(void **)(sent + 4))            // splice point is the root
        goto rebalance_done;
    for (;;) {
        if (*(BYTE *)(edi + 0x2C) != 1)         // splice point is red
            goto rebalance_done;
        eax = (char *)*(void **)esi;
        if (edi != eax)                         // edi is the right child
            goto edi_right;
        eax = (char *)*(void **)(esi + 8);
        if (*(BYTE *)(eax + 0x2C) == 0) {       // sibling is red
            *(BYTE *)(eax + 0x2C) = 1;
            *(BYTE *)(esi + 0x2C) = 0;
            NameMapRotateLeft(obj, esi);
            eax = (char *)*(void **)(esi + 8);
        }
        if (*(BYTE *)(eax + 0x2D) != 0)         // sibling is the sentinel
            goto next;
        edx = (char *)*(void **)eax;            // sibling->left
        if (*(BYTE *)(edx + 0x2C) == 1) {       // left is black
            edx = (char *)*(void **)(eax + 8);  // sibling->right
            if (*(BYTE *)(edx + 0x2C) != 1)     // right is red
                goto final_left;
            goto both_black;
        }
        edx = (char *)*(void **)(eax + 8);
        if (*(BYTE *)(edx + 0x2C) != 1)         // right is red
            goto final_left;
        // left red, right black: rotate the sibling right first, then the
        // final rotation.
        *(BYTE *)((char *)*(void **)eax + 0x2C) = 1;   // left = black
        *(BYTE *)(eax + 0x2C) = 0;                        // sibling = red
        NameMapRotateRight(obj, eax);
        eax = (char *)*(void **)(esi + 8);            // reload the sibling
final_left:
        dl = *(BYTE *)(esi + 0x2C);
        *(BYTE *)(eax + 0x2C) = dl;
        *(BYTE *)(esi + 0x2C) = 1;
        eax = (char *)*(void **)(eax + 8);
        *(BYTE *)(eax + 0x2C) = 1;
        NameMapRotateLeft(obj, esi);
        goto rebalance_done;
edi_right:
        if (*(BYTE *)(eax + 0x2C) == 0) {       // sibling is red
            *(BYTE *)(eax + 0x2C) = 1;
            *(BYTE *)(esi + 0x2C) = 0;
            NameMapRotateRight(obj, esi);
            eax = (char *)*(void **)esi;
        }
        if (*(BYTE *)(eax + 0x2D) != 0)         // sibling is the sentinel
            goto next;
        edx = (char *)*(void **)(eax + 8);
        if (*(BYTE *)(edx + 0x2C) == 1) {       // right is black
            edx = (char *)*(void **)eax;
            if (*(BYTE *)(edx + 0x2C) != 1)     // left is red
                goto final_right;
            goto both_black;
        }
final_right:
        edx = (char *)*(void **)eax;
        if (*(BYTE *)(edx + 0x2C) == 1) {       // left is black
            edx = (char *)*(void **)(eax + 8);  // sibling->right
            *(BYTE *)(edx + 0x2C) = 1;
            *(BYTE *)(eax + 0x2C) = 0;
            NameMapRotateLeft(obj, eax);
            eax = (char *)*(void **)esi;
        }
        dl = *(BYTE *)(esi + 0x2C);
        *(BYTE *)(eax + 0x2C) = dl;
        *(BYTE *)(esi + 0x2C) = 1;
        eax = (char *)*(void **)eax;
        *(BYTE *)(eax + 0x2C) = 1;
        NameMapRotateRight(obj, esi);
        goto rebalance_done;
both_black:
        *(BYTE *)(eax + 0x2C) = 0;
next:
        edi = esi;                              // walk up one level
        esi = (char *)*(void **)(esi + 4);
        if (edi == *(void **)(sent + 4))
            goto rebalance_done;
    }
rebalance_done:
    *(BYTE *)(edi + 0x2C) = 1;
free_node:
    // Destroy the in-tree wide-string payload, then free the node.
    if (*(DWORD *)(N + 0x28) >= 8u)             // heap buffer
        LHFree(*(void **)(N + 0x14));
    *(DWORD *)(N + 0x28) = 7;                   // cap  = the reset sentinel
    *(DWORD *)(N + 0x24) = 0;                   // len  = 0
    *(WORD *)(N + 0x14) = 0;                    // buffer low word = 0
    LHFree(N);
    {
        unsigned int count = *(DWORD *)(m + 0x1C);
        if (count > 0)
            *(DWORD *)(m + 0x1C) = count - 1;
    }
    *(void **)((char *)out + 4) = node;
    *(void **)(out) = *(void **)(m);
    return out;
}

// sub_5AB5F0 (0x5AB5F0 - 0x5AB6BF) -- the name-map range erase (the
// MSVC debug std::map::erase(first, last) / _Erase range; the target
// of this task).  thiscall + `retn 0x14` = 5 stack args (bound0, out,
// start, bound1, end), declared object-first.  Validates `bound0`
// (and `bound1` on the fast path) against the tree head (`__invalid_
// parameter_noinfo` -> F3InvalidParam when a bound is null or a
// different node pointer).  Fast path: `start` at the sentinel's
// leftmost and `end` at the sentinel frees the whole root subtree via
// NameMapFreeSubtree, self-loops the sentinel and zeroes the count,
// then writes the end iterator `{head, sentinel}` into `out`.
// Otherwise it loops: `NameMapIterNext` advances the in-order
// iterator `start` (guarded by a bound0 / bound1 head check per
// iteration), and `NameMapEraseNode` (with a scratch 2-dword out)
// splices + rebalance-erases the old node, decrementing the count.
// When `start` reaches `end` it writes `{head, end}` into `out` and
// returns `out`.  [NOT byte-identical] placement-dependent (the E8
// calls to F3InvalidParam, NameMapFreeSubtree, NameMapIterNext and
// NameMapEraseNode).
void *NameMapEraseRange(void *obj, void *out, void *bound0, void *start,
                        void *bound1, void *end)
{
    char *m        = (char *)obj;
    char *sent     = (char *)*(void **)(m + 0x18);
    void *head     = *(void **)(m);
    char *leftmost = (char *)*(void **)sent;
    DWORD v14[2];

    if (bound0 == 0 || bound0 != head)
        F3InvalidParam();
    if (start != leftmost)
        goto generic;
    if (bound1 == 0 || bound1 != head)
        F3InvalidParam();
    if (end != sent)
        goto generic;

    // Fast path: erase the whole tree (free the root subtree).
    NameMapFreeSubtree(obj, *(void **)(sent + 4));
    *(void **)(sent + 4) = sent;
    *(DWORD *)(m + 0x1C) = 0;
    *(void **)sent = sent;
    *(void **)(sent + 8) = sent;
    *(void **)((char *)out + 4) = sent;
    *(void **)(out) = head;
    return out;

generic:
    for (;;) {
        if (bound0 == 0 || bound0 != bound1)
            F3InvalidParam();
        if (start == end) {
            *(void **)(out) = head;
            *(void **)((char *)out + 4) = start;
            return out;
        }
        void *oldStart = start;
        {
            void *it[2];
            it[0] = bound0;
            it[1] = start;
            NameMapIterNext(it);
            start = it[1];
        }
        NameMapEraseNode(obj, v14, bound0, oldStart);
    }
}
