# AffineLayer link ownership

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`, image base `0x140000000`. Virtual addresses below refer to this executable. `ScriptAffineLayerLinks` recovers the native link-state operations; it does not register an AffineLayer script class or schedule its matrix task.

## Native ownership and operations

Direct executable evidence establishes parent `ObjectInfo` at `+0x298`, child-array `ObjectInfo` at `+0x2b0`, cached self at `+8`, matrix-dirty byte at `+0x22c`, and the manager sort-dirty byte at `+0x38`. `SetLink` (`0x14005cbd0`) resolves proposed and existing native parents before comparing their pointers. Equal pointers return without changing links, arrays or dirty state; type-validation last-error effects still occur. This includes a stored expired weak parent versus proposed null.

The stored parent is a canonical weak reference. The children array holds **strong instances**. Native `0x140227ea0` obtains the weak reference, but `0x140228340` resolves it before `0x140221730` appends the raw top-of-stack value. Unchanged instruction execution verifies the array's stored instance tags and reference-count increments. Calling the array a weak-child list would produce the wrong lifetime behavior.

`Unlink` (`0x140058ca0`) returns immediately when the stored parent resolves to no native owner, retaining its existing raw weak root. With a live parent, it removes the first matching child through native array comparison/removal and then clears the parent root. Survivor order is preserved. `DetachChildren` (`0x140058d50`) snapshots the array length, reads each current index, clears a child's back-link only when it resolves to this parent, then clears the entire array. Temporary child roots survive the back-link operation.

`Invalidate` (`0x14005d000`) marks the current matrix dirty and recursively traverses the current child array under the original length snapshot. `AssignLinks` (`0x140059110`) first copies the resolved parent relationship, snapshots source length and reads live source indices while reparenting each obtained child. Reparenting removes an element from that same source array: an original `[A,B,C,D]` can transfer `A,C` and leave `B,D`. Failed later index reads push null before type lookup and preserve the resulting root-context last error. The implementation retains this measured behavior instead of copying all source children up front.

## Authored state and safety

The enclosing actual native owner supplies a borrowed class identity, a side-effect-free registered-owner lookup and its matrix/sort invalidation operations. The state adds no self or class external root. Its child array and optional weak parent are separate VM roots. The caller invokes `Retire` during native derived cleanup, before three-matrix and common Layer retirement; default member destruction only retires the remaining root fields. The enclosing owner and VM must outlive these operations. This state alone is not an automatic script-instance release adapter.

Root-context errors match the native global-VM type lookup: noninstances produce `the object is not a class instance`, wrong ancestry produces `invalid type tag`, and matching instances with absent native payloads return null without adding an error. Registered owners remain resolvable after script class metadata starts finalizing. An independent lifetime fixture confirms that the strong child array keeps an instance alive after other strong values retire and that clearing the array releases it.

Limits of 65,536 children/visits and 256 levels bound unsafe native graphs. Proposed cycles are explicitly rejected; native unbounded cyclic recursion is not emulated. The safety parent-chain check does not change native error state. These limits and allocation failures are authored safety boundaries, not observations of native handling.

## Validation and remaining work

Bounded execution runs unchanged native SetLink, Unlink, DetachChildren, Invalidate and AssignLinks bodies, ObjectInfo conversion/push/pop/type helpers, and actual ArrayAppend/Remove/Get/Clear on prepared VM/class/instance/array owners. Existing manager identity, external RefTable operations and error-string assignment are declared observers. The native corpus has 105 prepared graphs, 6,422 operations and 3,961,409 instructions. Projection SHA-256: `c112295d64c24b4243ac4b188295ea90059d4e8384171f55ba4f865ec32d6cd4`.

The authored implementation matches 1,255,184 before/after state and error words across 6,404 ordinary/assignment records with no differences; the separately prepared expired-parent case has independent lifetime/error fixtures. Comparison-vector SHA-256: `adb1398cc28a02025ebf1884e33de6d03bc1e5385ad136a34ce0156ca63eb9bc`. Four independent ASan/UBSan groups check strong/weak ownership, live assignment and recursive invalidation, expired-parent/error ordering, and bounded retirement. The comparison excludes native physical reference-count words from its flattened state vectors; those are observed by native stored-tag/count assertions and the separate authored lifetime fixture.

Genuine AffineLayer factory/class captures, full release preflight, intrusive manager membership/sorting, matrix callbacks, Process/frame integration, picking and native game-frame pixels remain open. This helper does not close those acceptance items.
