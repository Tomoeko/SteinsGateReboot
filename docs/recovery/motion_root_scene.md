# Motion root scene input

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are executable virtual addresses; native analysis explicitly selected `candidate:view:1:UEU`, assembly `view:1`.

`MotionRootScene` owns the real player's global root overrides and their evaluated composition input. It supplies `MotionHierarchyState` through the existing first-party transform evaluator. It recovers concrete root composition, rather than storing script properties in a generic bag. It supplies no script class, fake task, PSB root, selected hierarchy or draw callback.

## Native fields and initial-state correction

Player setup `0x14051a400` inserts one root node (`0x418` bytes) into player vector `+0x1f0`, with count `+0x1e8 = 1`. The root contains independent override and evaluated blocks:

| State | Root offsets |
| --- | --- |
| Override dirty / visible / flipX / flipY | `+0x380 / +0x382 / +0x383 / +0x384` |
| Override coordinate XYZ | `+0x388 / +0x38c / +0x390` |
| Evaluated visible / flipX / flipY | `+0x356 / +0x357 / +0x358` |
| Evaluated coordinate XYZ | `+0x35c / +0x360 / +0x364` |
| Evaluated linear transform XX, XY, YX, YY | `+0x84 / +0x88 / +0x8c / +0x90` |
| Frame displacement XYZ | `+0x94 / +0x98 / +0x9c` |

**Initial script-visible override is false**, while initial evaluated visible is true. Wrapper getter `0x140278450` returns `0x14051b290(player) ^ 1`; that helper returns `!root+0x382`, so the property exposes the raw override. Setter `0x140280ce0` routes false to `0x140519ba0` (clear override) and true to `0x14051fd00` (set override). Prior owner-plan language that said root-visible=true conflated evaluated visibility with the script property.

The initial evaluated matrix is all zero. Temporary identity writes in `0x14051a400` precede zeroing the whole `0x418`-byte root and are overwritten. First real composition builds the matrix. Coordinates, displacement and player priority `+0x258` initially contain zero. Root override/evaluated zoom starts1, shear/angle0, flipsfalse, opacity255, neutral modulation `0x808080ff`; the constructor retains the default flip/shear/scale/rotate order.

## Controls and actual composition

Coordinate2 `0x14051eee0` receives its packed eight-byte point in RDX and changes only X/Y, preserving Z. Coordinate3 `0x14051ee80` receives the three float32 values in XMM1/2/3. Coordinate2/3 compare floating values and mark override dirty only when a component differs; a changed vector writes its affected components together. NaN compares unequal; equal signed zeros retain the stored representation. Wrapper left/top use coordinate2 through `0x14051ee30` and override getter `0x140510bd0`; Z uses coordinate3 after `0x140518c30`. Default Layer's spatial-capability flag is false, which enables these direct coordinate paths; spatial `position` conversion is separate and unsupported here.

Visibility helpers and flip helper `0x14051f080` likewise preserve the dirty flag on repeated values. Priority `0x14051f190` stores the raw float32 word at player `+0x258` and enumerates nested players through `0x140518b10`. Root-only enumeration is empty; propagation to selected nested players remains open.

Actual composition `0x14050c550` snapshots the previous evaluated coordinates, copies root overrides to evaluated fields, clears override dirty, evaluates the real ordered transform through `0x14050ed50`, and records current-minus-previous displacement. Assigned tick `+0x191` instead zeroes all displacement components; assignment is cleared at the player's composition tail, after this work. Source `Compose(tick_assigned)` therefore consumes the update owner's pre-clear flag. Root input uses `Hidden = !evaluated_visible`; changing an override alone does not alter the previous evaluated frame until composition.

The retained probe executes actual root-only `0x14050c550`, empty variable phase `0x14050e530`, root deformation gate `0x140520d80`, and original zero-angle sine/cosine paths `0x14057cfb0`/`0x14057d5a0`. The authored root uses the existing `EvaluateMotionTransform` implementation with the independently proved native default attributes/order. This is a concrete composition prerequisite for the mandatory player update service, not proof of complete selected-layer evaluation or publication.

## Validation and next work

The oracle runs real root setup, coordinate/visibility/flip/priority functions and composition over 256 independent players with 16 sequential control states each. It observes bounded short-string assignment, zero-owned temporary EH frame helpers, memset and zero-owned root-node copy. No original process, window, audio or GPU capture runs. 4,096 transitions executed 665,604 original/boundary instructions; an independent field/displacement/transform expectation found zero differences. Both original trigonometric helpers executed 1,280 times.

`root-scene-native.bin`: 319,500 bytes, SHA-256 `9e078c7c5bc7bef7f5b96515051c38f3776aabbf73f5f038065a6036e5ae3c16`. Semantic sequence SHA-256 `24aa75434230c185e1bc501ed0bfa306b724970a13b30331f919dfbc0fa3bba4`; constructor root-field SHA-256 `9d4e4f60d81c6d436b10ba7ca8855d8083fd1f44dba559663d7e6e7c45a8296f`. C++ tests stream bounded records, compare exact float32 state and cover initial visibility/matrix, Z preservation, repeated controls, signed zero, raw priority, displacement and finite composition guards. Strict C++20 syntax/format pass. Coherent public replay matches all 4,096 transitions. Integrated sanitizer and portable Release fixtures pass in the 274/249 checkpoint; earlier private staging remains frozen.

Open: actual selected hierarchy frame preparation/composition/publication; native velocity/attenuation; nonzero root angle/scale/shear control routes; generic variable/modifier grammar; spatial-position conversions; recursive priority; wrapper detach/pause/clone policy; actual task/script and frame-context ownership; asynchronous completion and measured pixel fidelity. Raw numeric controls preserve their native storage behavior, while composition explicitly rejects nonfinite coordinates outside the measured domain. The next bounded scene tranche is ordinary selected-layer visibility/parent traversal and actual packet publication using this recovered root input.
