// Fable 3 -- SubNode red-black tree implementation (engine/subnode.cpp).
//
// Re-implements the SubNode tree family's single-node erase (sub_625860)
// and the range erase built on top of it (sub_626770), plus the rotation,
// leftmost/rightmost, in-order iterator-advance, and subtree-free helpers
// they call.  Every function carries its original IDA address range and a
// `[NOT byte-identical]` note (each has E8 relative calls and/or high-VA
// references, so a naked-asm byte match is not possible).
//
// SubNode node layout (12-byte RB header + payload + color/flag):
//   +0x00  left   (void*)   (also the reverse in-order list link)
//   +0x04  parent (void*)
//   +0x08  right  (void*)   (the child list)
//   +0x0C  key (payload word)
//   +0x10  object (void*)   (always heap-allocated; LHFree'd on destroy)
//   +0x14  payload word
//   +0x18  payload word
//   +0x1C  entry array end (void*)  (heap; LHFree'd on destroy when non-null)
//   +0x20  entry array start (void*)
//   +0x24  payload word
//   +0x28  color (BYTE: 0 = red, 1 = black)
//   +0x29  sentinel flag (BYTE: non-zero marks the head/sentinel node)
//
// SubNode tree object (32 bytes):
//   +0x00  head     (a heap-allocated node, carried into the out-iterator)
//   +0x18  sentinel (the RB head node: left = leftmost, parent = root,
//                    right = rightmost, flag byte +0x29 set)
//   +0x1C  count    (node count, unsigned)

#include <stdexcept>

#include "subnode.h"
#include "common/common.h"   // LHAlloc, LHFree
#include "xlip.h"            // F3InvalidParam

// The original's invalid-iterator guard throws a std::out_of_range
// ("invalid map/set<T> iterator") after routing the exception through the
// CRT's source-string builder and the global STL report handler
// (dword_1DDCFC8).  That path is only reached on a sentinel node, which the
// game never passes here, so a plain std::out_of_range throw is
// functionally equivalent for the reachable code path.
static __declspec(noreturn) void SubNodeThrowOutOfRange(void)
{
    throw std::out_of_range("invalid map/set<T> iterator");
}

// ============================================================
//  SubNodeLeftmost  (0x61E870 - 0x61E88B)  -- leftmost node of the subtree
//  rooted at `node`
//  [NOT byte-identical] placement-dependent (the relative self-branch; no
//  other references, but the loop is not a single jump).
//
//  void *SubNodeLeftmost(void *node)   -- cdecl, one arg, returns the node
//
//  Walks the left child (+0x00) links, stopping at the last non-sentinel
//  node (the sentinel has its flag byte at +0x29 set).
// ============================================================
static void *SubNodeLeftmost(void *node)
{
    char *cur = (char *)node;
    for (;;)
    {
        char *left = (char *)*(void **)cur;
        if (*(BYTE *)(left + 0x29) != 0)      // the sentinel
            break;
        cur = left;
    }
    return cur;
}

// ============================================================
//  SubNodeRightmost  (0x61E850 - 0x61E86C)  -- rightmost node of the
//  subtree rooted at `node`
//  [NOT byte-identical] placement-dependent (the relative self-branch).
//
//  void *SubNodeRightmost(void *node)   -- cdecl, one arg
//
//  Walks the right child (+0x08) links, stopping at the last non-sentinel
//  node.
// ============================================================
static void *SubNodeRightmost(void *node)
{
    char *cur = (char *)node;
    for (;;)
    {
        char *right = (char *)*(void **)(cur + 8);
        if (*(BYTE *)(right + 0x29) != 0)      // the sentinel
            break;
        cur = right;
    }
    return cur;
}

// ============================================================
//  SubNodeIterNext  (0x61EBC0 - 0x61EC23)  -- advance a red-black tree
//  in-order iterator to its successor (in place)
//  [NOT byte-identical] placement-dependent (the __invalid_parameter_noinfo
//  IAT-slot calls).
//
//  void SubNodeIterNext(void *iter)   -- thiscall, plain retn
//
//  `iter` is an adjacent {tag, node} pair (the original passes the address
//  of the caller's two stack slots).  tag (iter+0) must be non-zero; node
//  (iter+4) must not be the sentinel.  When node has a real right child the
//  successor is the leftmost node of that right subtree; otherwise it walks
//  the parent chain (+0x04) up until the first ancestor that is not a right
//  child (or the sentinel).
// ============================================================
static void SubNodeIterNext(void *iter)
{
    if (*(void **)iter == 0)
        F3InvalidParam();
    char *np = (char *)iter + 4;                 // &node
    void *node = *(void **)np;
    if (*(BYTE *)((char *)node + 0x29) != 0) {   // node is the sentinel
        F3InvalidParam();
        return;                                   // original: jmp (no return)
    }

    void *right = *(void **)((char *)node + 8);
    if (*(BYTE *)((char *)right + 0x29) == 0) {  // a real right child
        *(void **)np = SubNodeLeftmost(right);   // successor = leftmost(right)
    } else {                                     // the sentinel right child
        void *anc = *(void **)((char *)node + 4);
        for (;;) {
            if (*(BYTE *)((char *)anc + 0x29) != 0)
                break;                            // ancestor is the sentinel
            if (*(void **)np != *(void **)((char *)anc + 8))
                break;                            // node is not the right child
            *(void **)np = anc;
            anc = *(void **)((char *)anc + 4);
        }
        *(void **)np = anc;
    }
}

// ============================================================
//  SubNodeRotateLeft  (0x6208C0 - 0x62090E)  -- red-black tree rotate-left
//  [NOT byte-identical] placement-dependent (refs the tree's sentinel at
//  obj+0x18 for the root update).
//
//  void SubNodeRotateLeft(void *obj, void *x)   -- thiscall + retn 4
//
//  Standard MSVC _Rb_tree rotate-left of node `x` (y = x->right): y takes
//  x's place, x becomes y's left child.
// ============================================================
static void SubNodeRotateLeft(void *obj, void *x)
{
    char *X  = (char *)x;
    char *Y  = (char *)*(void **)(X + 8);        // y = x->right
    char *Yl = (char *)*(void **)Y;              // y->left

    *(void **)(X + 8) = Yl;                      // x->right = y->left
    if (*(BYTE *)(Yl + 0x29) == 0)
        *(void **)(Yl + 4) = X;                  // y->left->parent = x

    char *xp = (char *)*(void **)(X + 4);        // x->parent
    *(void **)(Y + 4) = xp;                      // y->parent = x->parent

    char *sent = (char *)*(void **)((char *)obj + 0x18);
    if (X == *(void **)(sent + 4)) {             // x was the root
        *(void **)(sent + 4) = Y;                // root = y
        *(void **)Y = X;                         // y->left = x
        *(void **)(X + 4) = Y;                   // x->parent = y
    } else if (X == *(void **)xp) {              // x was the left child
        *(void **)xp = Y;                        // parent->left = y
        *(void **)Y = X;                         // y->left = x
        *(void **)(X + 4) = Y;                   // x->parent = y
    } else {                                     // x was the right child
        *(void **)(xp + 8) = Y;                  // parent->right = y
        *(void **)Y = X;                         // y->left = x
        *(void **)(X + 4) = Y;                   // x->parent = y
    }
}

// ============================================================
//  SubNodeRotateRight  (0x61E8B0 - 0x61E902)  -- red-black tree
//  rotate-right
//  [NOT byte-identical] placement-dependent (refs the tree's sentinel at
//  obj+0x18 for the root update).
//
//  void SubNodeRotateRight(void *obj, void *x)   -- thiscall + retn 4
//
//  Mirror of SubNodeRotateLeft (y = x->left).
// ============================================================
static void SubNodeRotateRight(void *obj, void *x)
{
    char *X  = (char *)x;
    char *Y  = (char *)*(void **)X;              // y = x->left
    char *Yr = (char *)*(void **)(Y + 8);        // y->right

    *(void **)X = Yr;                            // x->left = y->right
    if (*(BYTE *)(Yr + 0x29) == 0)
        *(void **)(Yr + 4) = X;                  // y->right->parent = x

    char *xp = (char *)*(void **)(X + 4);        // x->parent
    *(void **)(Y + 4) = xp;                      // y->parent = x->parent

    char *sent = (char *)*(void **)((char *)obj + 0x18);
    if (X == *(void **)(sent + 4)) {             // x was the root
        *(void **)(sent + 4) = Y;                // root = y
        *(void **)(Y + 8) = X;                   // y->right = x
        *(void **)(X + 4) = Y;                   // x->parent = y
    } else if (X == *(void **)(xp + 8)) {        // x was the right child
        *(void **)(xp + 8) = Y;                  // parent->right = y
        *(void **)(Y + 8) = X;                   // y->right = x
        *(void **)(X + 4) = Y;                   // x->parent = y
    } else {                                     // x was the left child
        *(void **)xp = Y;                        // parent->left = y
        *(void **)(Y + 8) = X;                   // y->right = x
        *(void **)(X + 4) = Y;                   // x->parent = y
    }
}

// ============================================================
//  SubNodeFreeSubtree  (0x625C40 - 0x625C99)  -- free every node of the
//  red-black subtree rooted at `node` (the shallow node destroy)
//  [NOT byte-identical] placement-dependent (the recursive call) and refs
//  the heap table at its original high VA (the node frees go through it,
//  i.e. LHFree).
//
//  void SubNodeFreeSubtree(void *obj, void *node)   -- thiscall + retn 4
//
//  Frees the subtree rooted at `node`: recurse into the node's right
//  subtree (+0x08), free the node's +0x1C entry array when non-null (with
//  no per-entry release -- the per-entry vtable release variant is
//  SubNodeTreeDtor 0x625BB0 in gamethread.cpp), zero +0x1C / +0x20 / +0x24,
//  free the +0x10 object and the node itself, then advance to the node's
//  left child (+0x00) and repeat.  Stops (without freeing) at the sentinel.
// ============================================================
static void SubNodeFreeSubtree(void *obj, void *node)
{
    for (;;)
    {
        char *n = (char *)node;
        if (*(BYTE *)(n + 0x29) != 0)            // the sentinel
            break;
        SubNodeFreeSubtree(obj, *(void **)(n + 8));  // the right subtree
        void *arr = *(void **)(n + 0x1C);
        if (arr != 0)
            LHFree(arr);
        *(void **)(n + 0x1C) = 0;
        *(void **)(n + 0x20) = 0;
        *(void **)(n + 0x24) = 0;
        LHFree(*(void **)(n + 0x10));            // the node's object
        LHFree(node);                            // free this node
        node = *(void **)n;                      // advance to the left child
    }
}

// ============================================================
//  SubNodeEraseNode  (0x625860 - 0x625BA9)  -- erase one red-black tree node
//  [NOT byte-identical] placement-dependent (the relative calls to the
//  iterator-advance / leftmost / rightmost / rotate helpers and LHFree) and
//  refs the high-VA exception machinery (dword_1DDCFC8, the std::out_of_range
//  / logic_error vtables, sub_402F00 / sub_401BD0 / std::_Throw) -- the
//  invalid-iterator throw is simplified to a std::out_of_range.
//
//  void *SubNodeEraseNode(void *obj, void *out, void *tag, void *node)
//
//  Original: thiscall + `retn 0xC` (obj in ECX, the 8-byte {tag, node}
//  out-iterator scratch in EDX, the erase `tag`/`node` pair on the stack).
//  `node` must not be the sentinel (std::out_of_range otherwise).  It
//  advances the {tag, node} iterator to the in-order successor, unlinks the
//  node from the tree (relinking the successor into its place when it has
//  two children and swapping their colors), rebalances when the removed
//  node was black, destroys the node's payload (the +0x1C array, the +0x10
//  object) and frees the node, decrements the tree's count (+0x1C), and
//  writes {head, successor} to the out-iterator (returned).
// ============================================================
static void *SubNodeEraseNode(void *obj, void *out, void *tag, void *node)
{
    char *m    = (char *)obj;
    char *sent = (char *)*(void **)(m + 0x18);    // the sentinel head node

    if (*(BYTE *)((char *)node + 0x29) != 0)
        SubNodeThrowOutOfRange();                // throws; does not return

    char *N = (char *)node;                      // the original node

    // Advance the {tag, node} iterator to the in-order successor.  The
    // original keeps the pair on the caller's stack (arg_4 / arg_8, 4 bytes
    // apart) and advances arg_8 in place; here it is staged in an adjacent
    // 2-dword local so the successor lands in `node` (used for out[1]).
    {
        void *it[2];
        it[0] = tag;
        it[1] = node;
        SubNodeIterNext(it);
        node = it[1];                             // node <- successor
    }
    char *S = (char *)node;                       // the successor

    char *left  = (char *)*(void **)N;            // N->left
    char *right = (char *)*(void **)(N + 8);      // N->right
    char *P;                                      // the spliced-in node
    char *parent;                                 // P's parent

    if (*(BYTE *)(left + 0x29) != 0 ||
        *(BYTE *)(right + 0x29) != 0) {
        // ---- one child (or a leaf): splice the child into N's place ----
        P = (*(BYTE *)(left + 0x29) != 0) ? right : left;
        parent = (char *)*(void **)(N + 4);
        if (*(BYTE *)(P + 0x29) == 0)
            *(void **)(P + 4) = parent;           // child->parent = N->parent

        // relink P into N's position
        if (N == *(void **)(sent + 4))            // N was the root
            *(void **)(sent + 4) = P;
        else if (N == *(void **)parent)           // N was the left child
            *(void **)parent = P;
        else                                       // N was the right child
            *(void **)(parent + 8) = P;

        // update the sentinel's leftmost / rightmost pointers
        if (N == *(void **)sent) {
            if (*(BYTE *)(P + 0x29) != 0)
                *(void **)sent = parent;
            else
                *(void **)sent = SubNodeLeftmost(P);
        }
        if (N == *(void **)(sent + 8)) {
            if (*(BYTE *)(P + 0x29) != 0)
                *(void **)(sent + 8) = parent;
            else
                *(void **)(sent + 8) = SubNodeRightmost(P);
        }
    } else {
        // ---- both children: S (= the successor) takes N's position ----
        *(void **)(left + 4) = S;                 // (N->left)->parent = S
        *(void **)S = left;                       // S->left = N->left
        if (S != right) {
            char *sp = (char *)*(void **)(S + 4); // S->parent
            char *sr = (char *)*(void **)(S + 8); // S->right
            if (*(BYTE *)(sr + 0x29) == 0)
                *(void **)(sr + 4) = sp;          // (S->right)->parent
            *(void **)sp = sr;                    // (S->parent)->left = S->right
            *(void **)(S + 8) = right;            // S->right = N->right
            *(void **)(right + 4) = S;            // (N->right)->parent = S
            parent = sp;
        } else {                                  // S is directly N->right
            parent = S;
        }
        P = (char *)*(void **)(S + 8);            // the hole is filled by S->right

        // relink S (in N's spot) against N's parent
        {
            char *Np = (char *)*(void **)(N + 4); // N->parent
            if (N == *(void **)(sent + 4))        // N was the root
                *(void **)(sent + 4) = S;
            else if (N == *(void **)Np)           // N was the left child
                *(void **)Np = S;
            else                                  // N was the right child
                *(void **)(Np + 8) = S;
            *(void **)(S + 4) = Np;               // S->parent = N->parent
        }

        // S takes N's color (N is freed below)
        {
            unsigned char c  = *(BYTE *)(N + 0x28);
            unsigned char cs = *(BYTE *)(S + 0x28);
            *(BYTE *)(N + 0x28) = cs;
            *(BYTE *)(S + 0x28) = c;
        }
    }

    // ---- red-black rebalance (the classic MSVC _Rb_tree_rebalance_after) ----
    // `P` is the node spliced into the hole, `parent` its parent.  Only when
    // the removed node was black (color 1; in the two-children case N now
    // holds S's original color).  The sentinel's children count as black and
    // a sentinel sibling is never recolored.
    if (*(BYTE *)(N + 0x28) == 1)
    {
        for (;;) {
            if (P == *(void **)(sent + 4))         // P is the root
                break;
            if (*(BYTE *)(P + 0x28) != 1)          // P is red
                break;

            char *W = (char *)*(void **)parent;    // the sibling starts as
                                                   // parent->left
            if (P == W) {                          // P is the left child
                W = (char *)*(void **)(parent + 8);
                if (*(BYTE *)(W + 0x28) == 0) {    // sibling red
                    *(BYTE *)(W + 0x28) = 1;
                    *(BYTE *)(parent + 0x28) = 0;
                    SubNodeRotateLeft(obj, parent);
                    W = (char *)*(void **)(parent + 8);
                }
                if (*(BYTE *)(W + 0x29) == 0) {    // sibling a real node
                    char *wl = (char *)*(void **)W;
                    char *wr = (char *)*(void **)(W + 8);
                    if (*(BYTE *)(wl + 0x28) == 1) {
                        if (*(BYTE *)(wr + 0x28) == 1)
                            goto recolor_w_red;   // both children black
                        goto case3_left;          // sibling's right is red
                    } else {
                        if (*(BYTE *)(wr + 0x28) == 1)
                            goto case2_left;      // sibling's right black
                        goto case3_left;          // sibling's right red
                    }
                }
            } else {                               // P is the right child
                if (*(BYTE *)(W + 0x28) == 0) {    // sibling red
                    *(BYTE *)(W + 0x28) = 1;
                    *(BYTE *)(parent + 0x28) = 0;
                    SubNodeRotateRight(obj, parent);
                    W = (char *)*(void **)parent;
                }
                if (*(BYTE *)(W + 0x29) == 0) {    // sibling a real node
                    char *wl = (char *)*(void **)W;
                    char *wr = (char *)*(void **)(W + 8);
                    if (*(BYTE *)(wr + 0x28) == 0)
                        goto case3_right;         // sibling's right is red
                    if (*(BYTE *)(wl + 0x28) == 0)
                        goto case3_right;         // sibling's left is red
                    goto recolor_w_red;           // both children black
                }
            }

recolor_w_red:
            if (*(BYTE *)(W + 0x29) == 0)
                *(BYTE *)(W + 0x28) = 0;          // sibling = red (only a
                                                   // real sibling is recolored)
            P = parent;                           // climb to the parent
            parent = (char *)*(void **)(parent + 4);
            continue;

case2_left:
            *(BYTE *)((char *)*(void **)W + 0x28) = 1;   // sibling's left = black
            *(BYTE *)(W + 0x28) = 0;                     // sibling = red
            SubNodeRotateRight(obj, W);
            W = (char *)*(void **)(parent + 8);
case3_left:
            *(BYTE *)(W + 0x28) = *(BYTE *)(parent + 0x28);
            *(BYTE *)(parent + 0x28) = 1;
            *(BYTE *)((char *)*(void **)(W + 8) + 0x28) = 1;   // sibling's right = black
            SubNodeRotateLeft(obj, parent);
            break;

case3_right:
            if (*(BYTE *)((char *)*(void **)W + 0x28) == 0)
                goto case3_right2;              // sibling's left red
            *(BYTE *)((char *)*(void **)(W + 8) + 0x28) = 1;  // sibling's right = black
            *(BYTE *)(W + 0x28) = 0;                     // sibling = red
            SubNodeRotateLeft(obj, W);
            W = (char *)*(void **)parent;
case3_right2:
            *(BYTE *)(W + 0x28) = *(BYTE *)(parent + 0x28);
            *(BYTE *)(parent + 0x28) = 1;
            *(BYTE *)((char *)*(void **)W + 0x28) = 1;   // sibling's left = black
            SubNodeRotateRight(obj, parent);
            break;
        }
        *(BYTE *)(P + 0x28) = 1;                 // mark the node black
    }

    // ---- destroy N's payload and the node ----
    {
        void *arr = *(void **)(N + 0x1C);
        if (arr != 0)
            LHFree(arr);
        *(void **)(N + 0x1C) = 0;
        *(void **)(N + 0x20) = 0;
        *(void **)(N + 0x24) = 0;
        LHFree(*(void **)(N + 0x10));            // the node's object
    }
    LHFree(N);                                   // free the erased node
    {
        unsigned int count = *(DWORD *)(m + 0x1C);
        if (count > 0)
            *(DWORD *)(m + 0x1C) = count - 1;
    }
    *(void **)((char *)out + 4) = node;          // out[1] = successor
    *(void **)(out) = *(void **)(m);             // out[0] = head
    return out;
}

// ============================================================
//  SubNodeEraseRange  (0x626770 - 0x626853)  -- the target: erase the
//  SubNode tree's in-order node range [start, end]
//  [NOT byte-identical] placement-dependent (the relative calls to the
//  subtree-free / iterator-advance / node-erase helpers and the
//  __invalid_parameter_noinfo IAT slot).
//
//  void *SubNodeEraseRange(void *obj, void *out, void *bound0, void *start,
//                          void *bound1, void *end)
//
//  Original: thiscall + `retn 0x14` (obj in ECX, five stack args).  The
//  bounds must match the tree's head (obj+0x00): `bound0`/`bound1` are each
//  non-null and == the head, `start` (== bound0 in the fast path) is the
//  sentinel's leftmost node, and `end` (== the sentinel) is the erase limit.
//
//  Fast path (start == leftmost && end == sentinel): free the whole tree
//  from the root, reset the sentinel to an empty self-referencing node,
//  clear the count, and write {head, sentinel} to the out-iterator.
//
//  Otherwise (the generic path): loop -- check the iterator tags match,
//  advance the {bound0, node} iterator to its successor and erase the
//  (previous) node -- until the running node reaches `end`, then write
//  {head, node} to the out-iterator.  Returns the out-iterator (the caller
//  discards the pointer).
// ============================================================
void *SubNodeEraseRange(void *obj, void *out, void *bound0, void *start,
                        void *bound1, void *end)
{
    char *m        = (char *)obj;
    char *sent     = (char *)*(void **)(m + 0x18);  // the sentinel head node
    void *head     = *(void **)(m);                  // obj+0x00
    void *leftmost = *(void **)(sent);               // sentinel->left
    char *v14[2];                                     // 8-byte out-iterator scratch

    // bound0 must be non-null and == the head
    if (bound0 == 0 || bound0 != head)
        F3InvalidParam();
    if (start != leftmost)
        goto generic;
    // bound1 must be non-null and == the head
    if (bound1 == 0 || bound1 != head)
        F3InvalidParam();
    if (end != sent)
        goto generic;

    // ---- fast path: clear the whole tree, return {head, sentinel} ----
    SubNodeFreeSubtree(obj, *(void **)(sent + 4));   // free the root subtree
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
            SubNodeIterNext(it);
            start = it[1];
        }
        SubNodeEraseNode(obj, v14, bound0, oldStart);  // erase the old node
    }
    *(void **)out = head;                              // out[0] = head
    *(void **)((char *)out + 4) = start;               // out[1] = node
    return out;
}

// ============================================================
//  InitSubNodeTree  (0x7281F0 - 0x728264)  -- the SubNode tree object init
//  [NOT byte-identical] placement-dependent (the E8 calls to LHAlloc).
//
//  void *InitSubNodeTree(void *obj)   -- thiscall + retn 8 in the original
//  (obj in ECX, two stack args unused), declared object-first here.
//
//  Initializes the 32-byte SubNode tree object in place: +0x00 = the
//  4-byte self-referencing head node (the block stores the tree object
//  pointer; NULL when the allocation fails), +0x18 = the freshly allocated
//  44-byte sentinel node (left / parent / right = the block itself -- the
//  RB head self-loop -- color +0x28 = black, sentinel flag +0x29 = 1, the
//  three self-reference dwords self-pointing at the block), +0x1C = the
//  node count (0).  The payload words (+0x0C..+0x27) are left untouched,
//  exactly as the original does.  The original's OOM path is a wild write
//  (it stores through a NULL sentinel unconditionally), so the OOM stand-in
//  stores NULL / leaves the count zeroed instead of trapping.
// ============================================================
void *InitSubNodeTree(void *obj)
{
    // +0x00: the 4-byte self-referencing head node.
    DWORD *head = (DWORD *)LHAlloc(4);
    if (head != 0)
        head[0] = (DWORD)obj;
    *(DWORD *)obj = (DWORD)head;

    // +0x18: the 44-byte sentinel node, +0x1C: the node count (0).
    DWORD *sent = (DWORD *)LHAlloc(44);
    *(DWORD *)((char *)obj + 0x18) = (DWORD)sent;
    *(DWORD *)((char *)obj + 0x1C) = 0;
    if (sent != 0)
    {
        sent[0] = 0;
        sent[1] = 0;
        sent[2] = 0;
        *(BYTE *)((char *)sent + 0x29) = 0;
        *(BYTE *)((char *)sent + 0x28) = 1;       // color = black
        *(BYTE *)((char *)sent + 0x29) = 1;       // sentinel flag = set
        sent[0] = (DWORD)sent;
        sent[1] = (DWORD)sent;
        sent[2] = (DWORD)sent;
    }
    return obj;
}
