# Native Motion child selection and reset

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Read-only Binary Ninja target: `candidate:view:1:UEU`. Addresses below are executable virtual-address suffixes relative to `0x140000000`.

## Recovered owner operations

`514ead` loads the parent's 16-byte preferred-root atom at `+150`; `514eb5` stores it into the newly constructed child at `+150`. The separate cached timeline at `+140`, selected timeline, clock and names are not copied by these instructions. Child commands retain the parent's actual resource manager and this lookup hint. Neither this hint nor a selected timeline borrows across final manager unregistration. No additional resource pin is claimed.

The existing recovered `ResolveTimeline` search uses this preferred project before registration order. Successful ordinary selection replaces the hint with the selected root. Failed selection clears `+150` while retaining cached `+140`, timing and names. Character changes preserve the hint while stopping and clearing current/requested motion names. Child construction therefore cannot reconstruct the hint from a surviving cached timeline after failed lookup.

`51f270` guards its numeric tick setter with root `+150`, not cached timeline `+140`. The new private clock assignment operation is shared with the standalone clock, whose existing timeline guard is retained. Commands use the actual preferred-root guard, permitting an initial child tick before timeline selection. `51a400` initializes assigned-tick `+191` to one, initial-tag `+193` to zero, and both numeric tick fields to zero. The actual SSE maximum with zero preserves positive infinity and converts negative, NaN and signed-zero inputs to positive zero. Evaluation time remains clamped to the initialized zero LastTime.

For a changed kind3 layer whose selected frame is hidden or has empty source, `521bf0` stops child `+190`, retires its selected tree through actual `50fd90`, then clears only the current/requested motion strings at `+90/+b0`. Current/requested/deferred character, deferred/delayed motion, cached timeline/preferred root, tick/evaluation/last/loop/speed, tick/tag flags and child queued-event storage survive. There is no child update or event transfer on this branch. Authored `ResetChildMotion` requires the actual selected-tree retirement operation; no empty default is accepted. An authored service exception leaves the child stopped with motion names uncleared, without claiming native exception parity. JOIN snapshots and nontrivial selected-tree owners remain the service provider's explicit unsupported boundary.

## Validation and scope

Strict C++20 syntax/warnings and repository formatting pass for four source overlays and the focused test. Independent default tests cover copied preference against earlier duplicate registrations, null cache, failed lookup, cross-manager rejection, stop-before-retirement ordering, retained names/clock and required-service failures. Optional readers retain the existing 6,144 native ordinary-command transitions and add the 128 native child initial-tick vectors. These C++ executions are pending the parent's coherent linked validation; no private core library was linked.

The unchanged native `521bf0` hidden branch and `50fd90` root-only selected-tree retirement execute 128 cases, 24,832 instructions, with 128 actual retirement calls and zero independent order/names/clock/root/cache/queue differences. The unchanged `51f270` executes 128 initial-child root/cache/IEEE vectors, 4,288 instructions, with zero differences. Receipts and small fixtures remain private.

The following producer remains open: parent root pose, supplied matrix and direction coupling, inherited clip/projection/stencil ownership, selected resource lifetime across script unregister, and actual kind3 selected-layer publication. Kind3 LocalFrames admission stays closed. These controls are a dependency of the genuine producer, not a Motion binder or child task alias.

## SQMotion listener receiver lifetime

Read-only `273d20` resolves the listener's stored receiver before looking up `onAction`, then pushes the closure, actual receiver, label and action strings before `222240` invokes the callback. `042d00` and `228140` explicitly resolve weak-reference tag `0x8010000` via `223640`. `224170` briefly retains its local object copy; `25ac20` writes the VM stack slot and increments the referenced object, then the local temporary release leaves the real VM stack root. `223640` likewise pushes and retains the weak target on the VM stack. This proves temporary actual receiver ownership during the call; it does not prove a permanent listener/self/resource pin. `227580/227690` retain/release the captured closure. Arguments converted through `228510` are VM-owned SQString values for that callback.

The native Action dispatcher `51b4e0` iterates a live listener vector, and `51e640` detaches raw 24-byte queued event entries without copying resource strings. Final resource-cache release can synchronously evict backing bytes. Later queued borrowed views, listener mutation, wrapper retirement after stack-root release and callback unregister remain unresolved. `273990` marks the associated task status two for manager retirement; it does not establish an extra permanent wrapper/resource reference.

Coherent integrated ASan/UBSan and portable Release fixtures pass at the 308/283 checkpoint. Native replay acceptance applies only to the bounded behavior documented here; complete resource, Motion, font and game fidelity remain open.
