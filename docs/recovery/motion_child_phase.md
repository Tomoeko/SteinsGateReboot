# Native child timing and frame controls

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Read-only Binary Ninja `candidate:view:1:UEU` / `view:1`; bounded unchanged-native instruction probes, no original process or metadata mutation.

## Actual owner and phase

`0x140514d76` creates one real0x500-byte MMotionPlayer via `0x140506e50`, owns it through the8-byte node+0x410 holder and allocates two zeroed32-byte controls at node+0x228/+0x348. It shares top owner+0x18, links parent+0x20, copies preferred PSB root+0x150 and propagates priority/filter/emissive settings. Child root+0x419 receives its real parent matrix. `0x14051016b` destroys the child through its virtual destructor before freeing its holder and both controls; no standalone child task is created.

Ordinary composition50c550 invokes kind-3 phase521bf0. After selection/root binding, this calls524910(child,parent+0x1a4). The child resets its current preparation range, handles its deformation service when present, calls51ffd0 with parent-supplied scaled delta, then calls50c550. The clock multiplies that delta by child speed. Manager delta, delayed-play entry, task registration, parent packet publication, event delivery and draw do not run here.

521bf0 inserts queued24-byte child entries at parent.begin via500720, then clears child.end to child.begin. Multiple child groups therefore appear in reverse child grouping, with each group's order preserved, before existing parent events. The helper copies raw kind/label/action entries and preserves pointer identity; it does not copy or retain strings. Resource unregistration may synchronously evict backing. No callback-unregister string pin is inferred.

## Authored boundary

`MotionPlayerUpdate::UpdateChild(parent_delta_ticks)` shares the recovered clock/tag/preparation/composition core with ordinary Update and leaves its events queued. Constructor services remain mandatory. `PrependChildEvents` accepts transfer during actual parent composition, rejects self-transfer, delivery and child-update reentrancy, checks parent capacity before mutation, inserts at begin and clears the child queue. These safety checks are authored bounded-domain guards.

Kind3 MotionLocalFrames admission stays closed. Actual inherited root/matrix/pose and directional binding, source-character/timeline selection and hidden/empty-source reset owners still need coupling. Fresh portable command construction also does not yet implement the native copied parent preferred-root selection. No child task/Motion/binder alias is supplied.

## Optional control writes

Native514460 decodes the real frame header;512e40 prepares its attributes. Outer mask0x80000 selects `motion`. Low-byte inner mask bits select flags1, direction/dt2, completion/docmpl4, direction offset/dofst8 and target/dtgt16. Present motion objects reset defaults to flags0/dt1/dofst0/docmplfalse/empty target and always read float32 timeOffset.

Hidden and no-motion frames perform no write to reused control slots; only initial construction zeroes them. `PrepareMotionChildFrameControl` returns an optional write, and `ApplyMotionChildFrameControl` preserves the actual retained slot when absent. DirectionTarget borrows immutable raw frame/resource metadata through every selected use; raw rvalues are rejected. Specified records initialized metadata and is not a native packed field.

The parser does not execute direction modes or admit arbitrary play flags. Numeric integer/float fields preserve existing finite bounded conversion limits; boolean fields preserve full integer nonzero and IEEE nonzero/NaN conversion. Metadata/raw-resource ownership and ordinary versus packed/division interpretation remain separate.

## Validation

- All staged source/headers/test source pass strictC++20 syntax and format. Independent fixtures cover child phase isolation/speed, delayed-play preservation, reversed child grouping, atomic queue rejection, transfer during parent composition, rejection during delivery, metadata defaults/invalid fields and retained-slot writes.
- Actual524910:4096updates /3373712instructions, zero independent state/tag/phase/event differences. Mandatory composition is a measured boundary. Fixture SHA-256 `8a9fe0ed6a593f73523a7fea3da130db839b61387382bebce2522547a1e73ae6`.
- Actual512e40:128vectors plus186slot-retention checks /665084instructions, zero differences. O1ASan/UBSan metadata defaults and all128vectors pass. Fixture SHA-256 `01700b011d390480f576485b6de5cb1e647404e13074abf7958b668a6e25a00c`.
- Actual500720:256insertions /28007instructions, zero full-entry order/pointer differences; preallocated capacity, two child groups, zero/nonzero counts.
- Coherent C++ timing/event defaults and4096-vector replay pass using genuine manager/cache/commands dependencies; ordinary4096-update replay also passes after the shared-core refactor. Metadata proof peak1292KiB; all verified temporary executable/test/Find/symbol products pruned. No full-core/library copies or links.

Complete child hierarchy/rendering, packet publication, task/binder construction, clone/JOIN/division/packed/variable/deformation paths and callback-unregister resource pins remain open.
