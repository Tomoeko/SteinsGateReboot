# Selected child initial tick and boundary traversal

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Read-only analysis uses explicit Binary Ninja `candidate:view:1:UEU`. Addresses below are executable virtual addresses. No original process, window or audio was run.

This tranche adds the actual child initial-tick producer and the ordinary timeline clock's initial boundary traversal flag. It uses the existing selected node, physical frame slot, control, clock and required update services. Kind 3 admission and the enclosing script Motion/task/draw owner remain closed.

## Producer and state

Assembly at `0x140521e3b` and `0x140521e49` tests child `+190` Playing and `+191` TickAssigned. These are not preferred-root tests; the subsequent numeric setter `0x14051f270` separately applies its existing preferred-root guard. If either producer guard is false, this phase makes no write.

The selected physical frame record supplies Time at `+114`; its actual control owner at node `+228` supplies TimeOffset at control `+18`. The producer computes `(parent.TickCount - current.Time) + control.TimeOffset`, in that float32 order. For negative parent delta with a nonnegative child loop position, `0x140521e82` through `0x140521eaf` repeatedly computes `(tick - LastTime) + LoopTime` while tick is at least LastTime. It then calls the real numeric setter. If the immediate parent is not TickAssigned, `0x140521ec6` sets child `+194`.

The containing producer at `0x140521d3c` supplies R14 from parent `+198` (TickCount), while the separately retained pointer at `+19c` feeds direction evaluation. The earlier isolated assignment witness manually supplied R14 from `+19c`; it proved the numeric operation for that supplied driver but did not establish the containing caller's field. The [selected child command producer](motion_child_frame_selection.md) now verifies full entry with independent parent TickCount/EvaluationTime, and the helper and replay inputs use TickCount. Parameterized timing remains a separate unrecovered owner.

`AssignMotionChildInitialTick` supplies only this phase. The caller must supply the actual present selected frame/control and the real immediate-parent clock. It does not choose/play a motion, construct or bind a root/pose, update a child, prepend its events or publish a draw packet. It rejects missing/hidden character frames, nonfinite timing and nonprogressing/excessive reverse wraps before writing the child. The default 65,536-wrap ceiling and finite-domain restrictions are authored safety limits, not native acceptance claims for malformed metadata.

## Ordered traversal

The ordinary `0x14051ffd0` path consumes `+194` only inside the existing pending-`+193` initial branch. Preexisting sync waiting returns before that branch and preserves both pending flags. The branch clears `+193` at `0x1405200fb`; its existing negative-delta/zero-tick special case first sets TickCount and EvaluationTime to LastTime.

At `0x14052012b` the clock tests `+194`, clearing it at `0x14052013e` before any additional seek. For nonnegative delta it saves the assigned EvaluationTime, seeks at zero, and, only if that seek has not reached a sync/end alignment stop, restores the assigned EvaluationTime and traverses forward. For negative delta below LastTime it performs the same sequence from LastTime and traverses backward. Negative delta at or beyond LastTime uses the ordinary current-position seek. Every endpoint seek and second traversal preserves the existing sync/alignment stop points before normal advancement.

A blocked endpoint seek therefore leaves EvaluationTime at the endpoint and consumes `+194`; it does not restore the assigned position or run the second traversal. An unconsumed request remains pending when `+193` is absent. Play, Stop and assignment keep their existing state transitions; requesting this flag does not perform a tick assignment or force composition.

`MotionPlayerUpdate::TraverseInitialPosition` expresses this ordering through the existing real `Traverse` service. Required preparation, composition and event delivery boundaries remain mandatory. The proof composes the real numeric root and leaves selected-frame dirty state pending: actual local geometry, child binding and draw publication are not falsely acknowledged.

## Discriminating checks

Two bounded native scripts stream the unchanged PE into an instruction emulator. They construct the real selected kind 3 node and its two control-slot owners and run the identified machine-code phases. Actual tag, selected seek/forward/reverse, control preparation and 24-byte queued event behavior execute natively. A supplied composition observer records final selected slots and clears only the proven clock assignment/initial-composition fields. It does not emulate missing geometry or pretend to run the whole renderer.

| Scope | Measured result |
| --- | --- |
| Initial traversal: 64 cases, four updates each | 256 updates; 4,508,358 native instructions; 70 endpoint bursts; 141 sync/alignment updates; zero independent differences |
| Initial assignment: actual node cell's exclusive child and decoded current control | 128 cases; 6,958,176 native instructions; five reverse-loop reduction cases; 64 guarded no-write cases; zero independent differences |
| Authored direct-source replay | O1 ASan/UBSan, strict warnings, independent default ordering/stop/safety fixtures and both native fixtures pass |
| Existing ordinary update regression | 4,096 native transitions pass against the new clock layout and default false flag |
| Existing child timing/event regression | 4,096 native transitions pass against the same new layout |

Traversal fixture: 35,061 bytes, SHA-256 `0478ba5d41c751688f2772deaf929703baca34ada8c8709082ac60b78fe680fb`; semantic SHA-256 `dcb8af07719b1fa081d996ea96e86c47501fb2c564656471e81b3e8e120f989b`. Assignment fixture: 5,128 bytes, SHA-256 `2a48d735754cc08882a11ef0d7ddd3d7f0a451eae6fe5180a7223f1394c88247`; semantic SHA-256 `d39e5611b42d2088b6c106b35be330980b40182fcc55964e5b27aba82c22d457`.

The independent C++ fixtures use genuine selected owners, current/following controls and frame indices, a real priority cursor, existing action queues and root composition. They distinguish endpoint bursts from ordinary seek, preserve blank layer actions and tag/action order, exercise reverse/zero-tick behavior, missing intermediate metadata and hidden final-slot reuse, and retain sync stop state and unconsumed flags. Event strings are borrowed from fixture resources held throughout detached delivery; this is not proof of production safety across callback unregister.

The isolated proof compiled 24 focused first-party translation units, including the exact existing `PsbValue::Find` definition from `src/resource/psb.cpp:497`, with source hashes retained privately. It used no whole-core copy or core library. Peak job storage was 12,976,675 bytes; generated executables and objects were hashed and retired after successful checks. Compiler/fixture/source receipts retain exact inputs.

## Remaining owner boundaries

Selected resource/event string backing across unregister remains unresolved: no new resource or persistent VM receiver pin is inferred. UID registry/listeners, alternate/packed/parameterized timelines, variables, meshes, geometry and inherited clip/projection/stencil producer coupling remain unsupported where the existing selected owner rejects them. Full kind 3 LocalFrames admission stays closed. The next containing producer must connect actual child selection/reset, root/pose/direction, ordered update/events and final geometry using their real owners before script Motion registration or task/draw attachment.

The 342/317 checkpoint passes coherent integrated sanitizer and portable Release checks, retained compatibility replays and the scoped actual owner integration. Native/static/source evidence boundaries and all unsupported full-game behavior remain as documented.
