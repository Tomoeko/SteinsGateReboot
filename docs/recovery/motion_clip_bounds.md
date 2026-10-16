# Motion clipping bounds recovery

Evidence date: 2026-10-12. Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses refer to the explicitly selected original PE Binary Ninja view. Original bytes and analysis metadata remain unchanged.

## CPU bounds behavior

`0x140526270` returns geometry bounds at `+0x28` through `+0x34`. On the first query, it sets cache byte `+0x38` before scanning borrowed XY float pairs between `+0x10` and `+0x18`. Empty geometry keeps its existing bounds. Calculated geometry is not rescanned. Each component uses original MINSS/MAXSS semantics: the second operand wins equal or unordered comparisons, including signed-zero and NaN payload choices. The authored geometry cache preserves this behavior and performs no allocation. A scene owner must keep positions and cache storage alive, reset them together when native invalidation requires it, and prepare bounds before ordinary rendering where possible. Native cache invalidation orchestration remains separate.

`0x140555380` unions geometry bounds from the supplied queue position onward. Hidden commands (`+0x04`) or zero-count commands (`+0x44`) are skipped without inspecting clip identity. The first eligible command with a different `+0x80` clip-group identity ends the run. At least one accepted command sets the result flag, even if its geometry bounds are reversed or unordered. Lazy geometry cache writes occur only for accepted entries.

`0x140555120` follows a clip-node parent chain at `+0x80`. Low mode bits equal to 2 skip that node. Bit 4 chooses the union of visible member commands from the node's `+0x88/+0x90` pointer range; this member path does not apply the queue's zero-count exclusion. Its COMISS/JB guard rejects reversed or unordered union bounds. Other modes query the visible node's own geometry without that validity guard. The first accepted node initializes the result; subsequent accepted nodes intersect it. A disjoint final intersection retains the accepted-result flag. `MotionClipBoundsResult` keeps both the four words and the flag rather than concealing those differences in a generic optional rectangle.

The portable views borrow geometry, position and member storage and keep clip identities explicit. Range/cycle checks and limits of 65,536 nodes and 1,048,576 command/member/uncached-position visits are authored safety bounds. They are not recovered game capacities. No Apple, Direct3D, GPU or audio API is used.

## Validation and remaining work

Independent ASan/UBSan fixtures cover empty/calculated caches, signed-zero and unordered second operands, hidden/empty command exclusions, visible same-group stopping, member union, parent intersection, unordered-union rejection versus direct-node acceptance, disjoint results, cycles and invalid parents.

Three thousand bounded cases executed the unchanged original helpers over finite values, signed zeros, infinities, quiet/signaling NaNs, cached/uncached geometry, aliases, hidden/empty commands and acyclic clip chains. All authored result flags, four result words and modified cache flags/bounds matched exactly. The 221,822-instruction trace SHA-256 is `96c9dc3458cc3f808fd44694148d42bd53230132d9b8a2ec11d0abe2c9a2b3b7`. No helper or foreign call boundary was substituted. Private vectors and the report remain ignored under `Files/research/runtime/`.

Floating-point exception flags remain unverified. Actual clipping policy, scene cache invalidation, stencil/mask phases, pixel coordinate expansion, batching and GPU output inside `0x140555610` remain open; these CPU bounds helpers do not establish native clipping fidelity.
