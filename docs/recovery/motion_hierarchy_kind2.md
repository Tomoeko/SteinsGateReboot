# Ordinary motion kind 2 hierarchy nodes

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Read-only Binary Ninja `candidate:view:1:UEU` / `view:1`; no metadata changes, binary patches or original process execution.

## Native behavior

- `0x1405146b0` reads serialized layer type into node+0x34. Its kind switch has no case 2; `0x14050dca0` performs ordinary initialization but places no kind-2 payload in node+0x410 and adds no kind-specific producer-list entry. This is an ordinary hierarchy node, distinct from the script `SQLayerFolder` task/child-manager owner.
- `0x140512e40` reads source/icon only for its kind mask `0x1849`, excluding 2. Numeric or absent source/icon fields therefore have no kind-2 preparation effect. The parser changes only kind 2; other unrecovered families retain their existing bounds.
- Seek `0x140511640`, forward `0x140518160` and reverse `0x14050a6b0` perform real selected-pair preparation/cursor changes and queue layer actions. Their `0x1801` source-binding dispatch excludes kind 2. Empty actions remain queued; no source callback is substituted.
- `0x14050bc40` applies the same ordinary cached local evaluation to kind 2 without a payload-specific phase. `0x14050c550` applies real parent/skip-ancestor, transforms, hidden-frame copying, visibility, hierarchy activity and dirty cleanup. Hierarchy activity does not promise a draw packet.
- Geometry `0x140520d80` uses masks `0x1c01` / `0x1401`, excluding 2. The full composition stencil-eligibility phase leaves node+0x3e9 false when `stencilType` is zero. Nonzero hierarchy stencil requires its actual owner and is rejected here. There is no sprite/source alias or synthetic task registration.
- Native `0x1404ca8a0` float32 decoding executes `MOVD` then `CVTTSS2SI` at `0x1404ca915` / `0x1404ca919`; float64 uses `CVTTSD2SI` at `0x1404ca8e0`. Finite values strictly between -1 and 1 convert to zero. The existing bounded numeric reader rejects nonfinite/out-of-range values rather than claiming native exceptional conversion parity.

## Authored delta and owner boundaries

Preparation records native `StencilType`, and kind-2 frames skip source-name decoding. The ordinary local owner accepts exact matching kind 0 / 2 raw and prepared layers; kind 2 additionally requires zero stencil in both raw/prepared metadata. Source binding is skipped only for kind 2, while mandatory actual binding/action services remain required for the selected owner and mixed trees. The conditional and forced numeric composition owners admit kind 2 with zero stencil; the current source-cache sprite builder continues to require kind 0.

Raw timeline, prepared timeline and optional immutable curve resource remain separate borrowed owners and outlive selected state, samples and queued action use. Prepared source/cache strings are not fabricated for kind 2. `CompleteComposition` still follows genuine required geometry/other producer phases for the complete selected tree. Publication orchestration must dispatch real producer families; it cannot treat every active hierarchy row as a sprite.

Unrecovered variables, layer parameterization, packed/division streams, mesh and ground corrections remain explicit failures. Kind 1/3/4/7/12 producers remain rejected here. Kind 3 owns a real child MMotionPlayer (`0x140514d76`), not this hierarchy-only path. UID/script lookup ownership, callback-unregister resource pins, renderer-context publication and the complete Motion script/task binder remain open.

## Validation

- Independent defaults cover missing and deliberately numeric unused source/icon, finite truncating-zero/nonzero stencil modes, forged raw/prepared stencil disagreement, mandatory services, excluded kinds, actual kind-2 parent/kind-0 child source identity, visibility versus enabled activity, cache selection/completion and failure publication.
- Actual PSB preparation plus selected cursors/cache/full composition: 1,536 transitions, 3,266,128 native instructions, zero gate differences; 576 suppressed evaluations, 384 tiny-ratio updates. All ratio, composed numeric, matrix, displacement, visibility and modulation words passed O1 ASan/UBSan C++ replay. Fixture SHA-256 `611fd92d87eb3b6ae90eb86ea1fe2b4220445670c4be56429159658019989f0b`.
- Actual cursor/action transitions: 2,048, 3,952,758 native instructions, zero independent differences, 288 explicitly empty actions and zero source-bind visits. O1 ASan/UBSan C++ replay passed. Fixture SHA-256 `69b6e1dc32b7968596a3d8daab2d3a21b323874211a0aefb68fc89ddf9f4c2e1`.
- Kind-2 skipped parent / kind-0 child probe: four full compositions, 6,118 native instructions, zero differences; changed immediate parent forces the child even when skipped for numeric inheritance.
- Full forced mixed kinds 2/0/2: 512 native updates / 1,536 layer outputs, 919,029 native instructions, zero independent visibility/modulation/hidden-parent differences. Native numeric fixture bytes equal the retained type-0 fixture SHA-256 `c327c4fea37f9566de3a01da7a9139fa7e9f8fd8be1c1e7061c968980edef102`; no duplicate fixture retained.

These are bounded ordinary hierarchy/source-not-ready domains. Coherent integrated sanitizer and portable Release checks pass at289/264. A preparation-only scan of137 original entries admits1,435 of5,796 timelines in the actual resource-backed ordinary sprite/hierarchy domain; it invokes no traversal/source/action/draw/upload/render/audio service and establishes no rendered frames. Strict syntax, format and bounded diagnostic fieldproof receipts are retained in ignored research storage; generated objects/executables are pruned after verification.
