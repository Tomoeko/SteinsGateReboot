# Motion resource recovery

Evidence date: 2026-10-12. The executable is `sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Binary Ninja analysis explicitly targeted its PE view. Addresses below are executable virtual addresses. Source files and original resources were read only.

## Complete archive coverage

The original `wind3d11data/motion_body.bin` has SHA-256 `a7b8b064a38f46597b97b7c6799d3ffbece0e9a86e6b037b6a80a4fdb1e63945`; its index has SHA-256 `699a81ba365a008e7262e824bb513bd266d0b6420069a7856b967492d5efec65`. All 137 entries passed first-party MZS/Zstandard decoding, PSB reading, and the motion resource reader. Total decoded container bytes processed were 3,313,234,304. Seven decoded containers exceed 64 MiB; the largest is 134,407,136 bytes. Analysis used a 160 MiB transient container limit and retained no full decoded installation.

The archives contain 5,796 motions, 43,549 layers, 268,059 generic frame records, 117 easing curves, and 32 coordinate Bezier paths. Observed layer types are 0, 1, 2, 3, 4, 7, and 12. Generic frame records, including priority tracks, have types 0 through 3. These numeric kinds are retained without assigning unsupported names to all layer types.

All 426 texture payloads decode successfully: 411 BC7 and 15 A8. Processing produced 13,005,556,992 RGBA bytes transiently. Motion atlases can have truncated dimensions smaller than their stored padded dimensions; UV and pixel stride must use the stored dimensions.

The `button` entry begins at motion body offset 29,533,714 and occupies 40,814 wrapped bytes. It decodes to 3,377,312 bytes with SHA-256 `d516db0bf04823293050cc332c5666946ea5a992507d964e2afd77841ba7cd9b`. Its atlas is 1928×1696 BC7 with truncated dimensions 1922×1690. The pixel chunk begins at decoded offset 107,424 and has length 3,269,888. It was retained only as a bounded private representative.

## Recovered structure

`id=motion`, `spec=win`, and version `3.03` identify these resources. Root `source` maps names to atlas textures and named icons. Icons store atlas rectangles, origins, and mesh references. Root `object` maps object names to named `motion` definitions. Each motion contains `lastTime`, `loopTime`, nested `layer` trees, `priority` frames, parameters, variables, and optional streaming information.

The reader preserves exact source/object/motion/layer names, transform order, inheritance masks, frame times/types, chunk ranges, and frame content. It retains other layer properties and mesh references for subsequent interpretation. It bounds arrays, aggregate frame/layer/timeline counts, recursion, numeric values, atlas rectangles, and curve sizes. Parser acceptance does not mean all retained options have executable implementations.

| Native address | Recovered behavior |
| --- | --- |
| `0x14051bd50` | Loads a motion's priority, tags, loop/last time, bounds, and parameterization. |
| `0x140514460` | Initializes layer frame descriptors. Type 0 sets the hidden byte; type 2 clears hidden/tween bytes; type 3 sets the tween byte with hidden clear. Nonempty content supplies its mask. Exact byte accesses were checked in assembly because inferred decompiler integer types obscure these adjacent flags. |
| `0x140512e40` | Initializes defaults and decodes masked frame attributes, source/icon identities, easing references, coordinate paths, mesh state, and layer-specific data. |
| `0x140511640` | Selects a frame at or immediately before the current time, then clamps the current index to at most `frameCount-2`. It prepares both current and following descriptors. |
| `0x14050bc40` | Evaluates prepared attributes. Tweening requires current tween enabled and both frames visible. Optional `interval` quantizes elapsed time before normalizing by the interval between keyframe times. |
| `0x14050c550` | Updates local state, selective/all transform inheritance, parent coordinate mapping, root normalization, byte opacity products, and inherited modulation. Dynamic overrides, ancestor selection, visibility controls, and mesh/reference state still need scene orchestration. |
| `0x14050dca0`, `0x140518160`, `0x14050a6b0`, `0x14050ad30` | Flatten layers depth first with parents preceding children; update active priority index arrays during forward/backward time changes; submit draw nodes by reversing those indices. |
| `0x14051f270` | Explicit seek clamps time to the interval from zero to `lastTime`. Playback loop and stopping behavior require additional player-state recovery. |
| `0x140504780`, `0x14051c910` | Read and evaluate scalar easing splines using knot arrays `x`, `y`, and `p`. `p` supplies second derivatives in the cubic spline correction. |
| `0x140504000`, `0x14051c5d0` | Read cubic Bezier controls `x/y`, segment times `t`, and per-segment spline remapping `s`; evaluate remapped path positions. |
| `0x140528530` | Uses remapped path X as forward travel and Y as perpendicular displacement in coordinate plane 0 (XY) or 1 (XZ), while the remaining coordinate interpolates linearly. Equal three-component endpoints bypass easing and path evaluation. |
| `0x1405146b0`, `0x14050ed50` | Read transform order and compose local flip, rotation, scale, and shear matrices. Order `[0,3,2,1]` has a dedicated path; other four-operation orders use successive matrix products. |
| `0x140528740`, `0x140528220`, `0x140528360` | Interpolate scalar values, shortest/directed angles, and packed color lanes respectively. Packed colors use an integer factor truncated from `ratio*256`. |

## Authored interpolation and differential checks

`EvaluateEasing`, `EvaluateMotionPath`, `InterpolateScalar`, `InterpolateAngle`, and `InterpolatePackedColor` implement the recovered operations. Ordinary evaluations allocate no memory and perform no I/O. Out-of-domain curve positions, malformed intervals, nonfinite values, and bounded-work violations produce errors rather than substitute animation.

Original x86 instructions were executed on synthetic descriptors in Unicorn 2.1.4, then compared with the authored Apple Silicon C++ implementation. The research tool was obtained from its [official PyPI release](https://pypi.org/project/unicorn/2.1.4/) and remains in ignored `Files/tools/unicorn-2.1.4/`, with version, provenance, and SHA-256 recorded. The downloaded ARM64 wheel hash is `d6c93e0f60328d8f4a1792af3f834137a28050fcc2305f2ec01efe8558a9844e`. It is not a runtime dependency. No executable code or analysis metadata was patched.

| Original operation | Synthetic samples | Differing output words |
| --- | ---: | ---: |
| Scalar easing spline | 1,001 | 0 |
| Scalar interpolation | 1,000 | 0 |
| Packed color interpolation | 1,000 | 0 |
| Angle interpolation, both transition modes | 2,000 | 0 |
| Remapped cubic Bezier path, two coordinates | 1,001 | 0 |

These are bitwise comparisons against instruction-level emulation, with floating-point contraction disabled. They validate the sampled formulas and operation order; they do not replace native game captures. Synthetic original-instruction spline vectors are retained in `tests/motion_tests.cpp`. Independent tests also cover metadata identity, texture padding, transform-order retention, terminal frames, malformed spline/path arrays, exact lookup names, nonfinite inputs, invalid timelines, and recursion limits. The suite passed AddressSanitizer and UndefinedBehaviorSanitizer.

## Prepared layer numerical evaluation

`PrepareMotionLayer` prepares common attributes and curve/path references once at load time. Every one of the original 43,549 layers passed preparation. `EvaluateMotionLayer` selects the current/following pair with the recovered `frameCount-2` clamp and implements hidden/hold/tween selection, elapsed-time quantization, coordinate/angle/zoom/property interpolation, rounded byte opacity, and packed corner colors. It borrows source/icon names from the prepared frames and allocates no memory during ordinary evaluation.

Numeric PSB values assigned to integer attributes use the original conversion behavior. `0x1404ca8a0` truncates floating values using `CVTTSS2SI` or `CVTTSD2SI`; this matters for original opacity fields stored as floating values. `0x1405823e0` rounds interpolated opacity to the nearest integer with half values away from zero. The default modulation color is `0x808080ff`, and native corner storage swaps exported corners 2 and 3 for strip order.

One hundred synthetic prepared-layer descriptors were evaluated by original `0x14050bc40` instructions and by the authored scene evaluator. Coordinates, four packed corner colors, opacity, angle, zoom, and stored `sx/sy` properties matched bit for bit across all 1,300 compared output words. A retained synthetic layer vector, frame-selection cases, quantization, corner order, and unresolved-mask checks passed sanitizers in `tests/motion_evaluator_tests.cpp`.

Coordinate-path evaluation matches original instructions across 200 synthetic descriptors (600 output words), covering XY and XZ planes, optional coordinate easing, equal components, and equal full vectors. All 32 original paths occur in XY layers. All 43,549 original layers use coordinate plane 0. Unsupported path planes produce explicit errors.

`EvaluateMotionTransform` implements the native local 2×2 linear transform. Public `ShearX/ShearY` names correspond to original `sx/sy` coefficients. Operations are 0=flip, 1=rotation, 2=scale, and 3=shear, applied in the stored order. The archive uses `[0,3,2,1]` in 43,541 layers and `[3,2,1,0]` in eight. All 114,564 visible original frame descriptors passed local transform evaluation.

The original SSE trigonometric branch at `0x14057cc40` and `0x14057d300` uses double argument reduction and polynomial evaluation, then rounds to float. Host trigonometric functions produced 23 differing matrix components across 240 initial descriptors; the recovered formulas eliminated these differences. All 9,600 matrix words matched across 2,400 synthetic descriptors spanning all 24 operation permutations and rotations within ±3,600,000 degrees. Five retained vectors and malformed-order/range checks passed sanitizers. Larger rotations are explicitly unsupported. The alternate AVX CRT dispatch branch remains unverified; these comparisons exercised the SSE branch.

`ComposeMotionHierarchyLayer` implements common hierarchy arithmetic for an already selected parent and resolved local overrides. All 8,000 child output words matched full original `0x14050c550` execution across 500 synthetic three-layer scenes without stubbing native helpers. The samples exercised partial/full inheritance, both root-normalization modes, coordinate mapping, matrices, stored transform attributes, flips, byte opacity, and inherited modulation. Three retained instruction vectors and packed modulation-color vectors are covered by independent tests. `normalize_against_root` corresponds to the original player byte at offset `0x418` being clear; recovering its script-facing control remains open.

`PrepareMotionTimeline` retains the native depth-first layer layout and parent indices. It prepares reversed priority orders once; `EvaluateMotionDrawOrder` borrows the selected order without allocation and clamps the active frame index to at most `priorityCount-2`. All 5,796 original timelines passed preparation and initial priority evaluation. Original `0x14050ad30` executed 100 synthetic seven-layer submission permutations; every submitted node order matched reversal of its active priority indices. Priority preparation checks track times, counts, and index ranges. It preserves repeated valid indices, as native submission does.

Unrecovered frame and inheritance mask bits remain visible in `UnresolvedMask` and `UnresolvedInheritMask`. Recovered deformation ancestor selection and geometry are described in [mesh recovery](motion_mesh.md). Dynamic overrides, visibility controls, reference playback, and complete hierarchy orchestration remain open.

## Undeformed sprite commands

`BuildMotionSpriteCommand` implements original type-0 undeformed geometry, stored atlas UVs, per-corner color modulation, opacity, and final texture flags. Icon lookup at `0x1405172e0` truncates `left/top/width/height` to native integers, retains float origins/resolution, and reads optional `attr`. Resolution scales sampled extents, while geometric width/height remain unscaled. All 11,564 original icons use resolution 1; their attributes are 0 in 7,852 icons, 1 in 3,420, and 2 in 292.

Geometry at `0x140520d80` subtracts the transformed sum of icon/frame origins from the evaluated coordinate; the caller supplies the native Z-to-Y projection coefficient. Atlas mapping at `0x140556c20` uses stored padded texture dimensions. Strip indices traverse top-left, bottom-left, top-right, bottom-right. Native batching adds duplicate first/last indices between strips. Command storage preserves row-major corners with an explicit strip index array.

The submission routine `0x14050ad30` applies byte opacity and inherited modulation, suppresses modulation RGB for blend operations 2, 3, and 5, clears texture alpha flags under recovered conditions, and adds the default-color optimization flag. The authored command builder matched original instructions across 500 geometry descriptors (4,000 float words), 200 UV descriptors (1,600 float words), and 200 color/flag descriptors (1,000 words). Retained synthetic instruction vectors, identity/lifetime checks, scaled/fractional rectangles, hidden layers, and unsupported-state checks passed sanitizers.

A bounded full-original metadata scan prepared every layer/timeline again and built 19,101 locally resolved undeformed sprite samples, including 5,644 `meshTransform=1` samples that use native no-patch quad fallback. It also built 1,555 local bicubic grid samples with a fixed 2×2 validation subdivision. Another 68,178 eligible samples referenced external or unresolved sources. This scan checked local command generation, not complete runtime parent/override resolution. There are 74,627 visible samples with `meshTransform=1`. Their frame bit `0x02000000`, bicubic control points, numerical interpolation, combined/inherited deformation-space geometry, and persistent native subdivision selection are decoded as described in [mesh recovery](motion_mesh.md). Project-scoped source/icon binding and original rename maps are recovered in [source lookup](motion_source_lookup.md). Shader/state selection is documented in [motion rendering](motion_rendering.md).

## Remaining acceptance checks

Complete hierarchy traversal with native visibility/override controls, remaining renderer command geometry, reference motion playback, runtime parameters, frame events, streamed motion chunks, dynamic source replacement, follow-offset controls, shapes, particles, and stencils remain incomplete. Icon mesh chunk records and bicubic deformation are decoded, while their shape/stencil submission paths and complete scene orchestration remain open. Scene draw order, looping/stopping, and timing still require native traces. Shader, sampler, blending, clipping, and GPU output comparisons must be performed alongside scene evaluation. Resource and numerical coverage must not be reported as complete animation or route fidelity.
