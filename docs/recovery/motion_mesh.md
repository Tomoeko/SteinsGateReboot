# Motion mesh and bicubic deformation recovery

Evidence date: 2026-10-12. Executable `sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Virtual addresses below belong to its explicitly selected Binary Ninja PE view. Original bytes and analysis metadata were unchanged.

## Original payload coverage

All 137 motion resources passed the mesh readers while streaming decoded containers under a 160 MiB transient limit. The 11,287 icon mesh objects contain 1,877,648 vertices and 4,795,007 strip indices. All inspected mesh matrices contain exactly 24 bytes. No complete decoded archive or collection of mesh dumps was retained.

All 74,627 frame mesh objects passed deformation metadata reading. The prepared evaluator also passed all 13,897 eligible original mesh tween midpoint checks. There are 15,082 explicit patches; the remainder omit control points. No original frame mesh has a non-null `cc` easing reference. Synthetic tests cover that supported field independently.

## Recovered fields and operations

| Native address | Behavior |
| --- | --- |
| `0x1405268e0` | Interprets icon `vertices` as float32 XY pairs with stride 8, `tristrip` as uint32 indices with stride 4, and `meshMatrix` as six float32 coefficients. |
| `0x140512e40` | For frame mask `0x02000000`, reads `mesh.cc` as an optional easing reference and `mesh.bp` as 32 float coordinates, forming 16 XY control points. |
| `0x140554b00` | Encodes two polynomial degrees in nibbles; `0x33` allocates a 4×4 control grid. |
| `0x14030c4c0`, `0x140554ce0` | Initialize identity-grid knots as `index/degree`, then append points in row-major order. Cubic identity controls therefore use coordinates 0, 1/3, 2/3, 1. |
| `0x14050bc40` | Hold frames copy their patch. Tween frames apply the current mesh easing, if present, before control interpolation. Missing endpoint patches use the identity grid. |
| `0x140542970` | Interpolates 32 cubic control lanes as `(end-start)*ratio + start`; copies endpoints when the absolute ratio or distance to 1 is strictly below `2^-23`. |
| `0x14053d150` | Evaluates bicubic patches at supplied coordinates. The SSE path multiplies Bernstein coefficients in a particular order and performs a pairwise final reduction. Coordinates outside [0,1] extrapolate; they are not clamped. |
| `0x140554dd0`, `0x14053c350` | Combine matching bicubic grids as `(first+second)-identity`, preserving float operation order. Degree conversion and ancestor selection remain separate operations. |
| `0x140526150` | Computes the inverse of a six-coefficient deformation-space matrix, caching it behind a dirty byte. |
| `0x140525c60`, `0x140554850` | Prepare transformed control points, convert geometry into deformation space, and apply the patch. Scene resource binding remains to be recovered. |
| `0x140520d80` | Builds the origin-corrected, full-rectangle deformation-space matrix and selects the own-patch, ancestor-patch, or undeformed grid path. |
| `0x1405146b0` | Reads mesh mode, child sync mask, division count, combinator presence, and `meshCombine`; the last property is node byte `+0x3db`. |
| `0x140554c60` | Splits an integer division count by width/height, with an extra increment after each minimum clamp. |
| `0x140554da0`, `0x14004d950` | Keep the maximum inherited division count at a fixed ratio; changing the ratio resets the minimum before accepting the new count. |
| `0x140512850` | Subdivides an undeformed quad with reciprocal steps, preserving a multiplication order distinct from cubic cached-grid evaluation. |
| `0x1405263a0` | Carries a stopping boundary's anchor through later ancestor warps and returns its displacement. |
| `0x14053fcb0`, `0x1405407e0` | Cache Bernstein weights in their original multiplication order and evaluate a regular grid over a supplied coordinate window. |
| `0x140512a30`, `0x140559960`, `0x14055a5b0` | Interpolate byte colors vertically then horizontally, pack positions/colors/UVs, and generate triangle lists; the 1×1 shortcut uses a four-index strip. |

Serialized `meshMatrix` coefficients `[a,b,c,d,tx,ty]` become native rows `[a,c,tx]` and `[b,d,ty]`. Original icon vertices are normalized source coordinates. The stored matrix maps those coordinates to local geometry; this alone does not define the complete animated deformation chain.

`ReadMotionIconMesh` decodes fields explicitly and validates chunk identity, byte ranges, strides, finite coordinates, counts, and strip indices. Normal and extra PSB chunks are supported. Hull, hole, and stencil metadata remains preserved in the existing icon metadata; this reader does not claim those behaviors are implemented.

`ReadMotionDeformation`, `IdentityMotionBezierPatch`, `InterpolateMotionBezierPatch`, `EvaluateMotionBezierPatch`, `CombineMotionBezierPatches`, and the affine inverse/control-point transforms implement the recovered data and numerical operations without allocation or I/O during evaluation. The prepared-layer evaluator decodes frame mesh bit `0x02000000` only for `meshTransform=1`, retains optional easing and control points, copies hold patches, and interpolates tween patches after time quantization. Other mesh transform modes retain unresolved bits.

## Differential and safety checks

Original x86 instructions were executed on bounded synthetic inputs using the ignored Unicorn research tool documented in [motion recovery](motion.md). Authored Apple Silicon results matched all 4,000 output words from 2,000 bicubic point evaluations and all 32,000 words from 1,000 control interpolations. Inputs included extrapolation, negative ratios, and endpoint tolerance boundaries. A further 500 same-degree patch combinations matched 16,000 words; 500 affine inversions matched 3,000 words; 500 sixteen-point affine transforms matched 16,000 words; and 100 complete original layer hold/tween executions matched all 3,200 patch words. These complete layer checks used preallocated native descriptors without replacing helpers. The comparisons exercised the SSE branch; the alternate AVX branch remains unverified.

Independent retained tests cover a synthetic native point vector, identity/default endpoints, easing, epsilon endpoint copies, malformed control counts, unaligned little-endian mesh bytes, normal/extra chunk identities, matrix reordering, native affine inverse/transform/combination vectors, singular inverses, invalid indices, truncated payloads, nonfinite values, and prepared hold/tween coupling. Targeted AddressSanitizer/UndefinedBehaviorSanitizer builds passed with compiler warnings treated as errors.

`MotionSpriteDeformationSpace`, `SelectMotionMeshSubdivision`, `EvaluateMotionBezierGrid`, and `BuildOwnMotionGridCommand` cover the resolved local bicubic sprite path. `PackMotionGridCommand` packs already-evaluated geometry with the same material, colors, UVs, and topology. The caller supplies the measured division count and preallocated position, vertex, and index buffers. Commands explicitly distinguish triangle lists from the 1×1 triangle-strip shortcut. The renderer uses regular grids for this path; icon polygon strips are not substituted for grid topology. A mesh-transform layer with no active own or inherited patch takes the native quad fallback.

A further 500 executions of the original complete geometry update matched all 3,000 deformation-space matrix words. All 50,822 words from 300 cached-grid evaluations and all 2,000 words from 1,000 subdivision selections matched. One hundred local grid commands matched 25,335 packed vertex words and 22,434 indices. These checks executed the original math, color interpolation, UV instructions, and final packing with bounded preallocated native descriptors; helpers were not replaced.

The complete archive scan additionally produced 19,101 locally resolved quad commands, including 5,644 mesh-transform layers with absent patches, and 1,555 locally resolved bicubic commands using an explicit test subdivision of 2×2. These are parser/evaluation checks, not claims about native scene tessellation or binding. Another 68,178 eligible samples reference sources outside their containing motion resource, requiring the scene owner’s resource bindings. Tests retain native packed vertex words, color interpolation order, UV arithmetic, topology, buffer lifetime, malformed buffer counts, and unsupported inherited-path rejection.

## Prepared hierarchy geometry and tessellation

`ResolveMotionDeformationLinks` recovers selected ancestors, active patches, child export, and stopping boundaries. Cleared inheritance bit `0x02000000` creates a stop when an ancestor exists. `meshSyncChildMask` bit 8 exports an active own patch. `meshCombine` joins literal parent chains; it is distinct from nested motion's `motionIndependentLayerInherit` setting.

`PreparedMotionGeometry` borrows caller-owned nodes, resolved frame geometry, links, and caches. It combines normalized bicubic patches, builds world deformation spaces, evaluates same-group reference windows or inherited bilinear grids, skips ancestors already included by combination, and applies stopping-boundary anchor offsets. The owner must keep borrowed state unchanged until the view is discarded. Preparation and evaluation allocate no memory or perform I/O. The sprite frame adapter explicitly rejects unresolved fields, non-sprite geometry, and combinator variants.

All 11,808 ancestor/flag outputs from 500 original scenes matched, as did all 7,000 words from 500 deformation-space applications. Three hundred bilinear grids matched 21,228 words. Two hundred complete original three-layer geometry executions matched every one of 31,606 position words, covering patch combination, reference-size differences, inherited deformation, and stop offsets. Original helpers ran against preallocated descriptors, vectors, and caches; none were replaced. A second set of 200 complete original scenes used mesh-transform-zero descendants and persistent division caches; all 30,536 position words and 1,200 subdivision values matched. Retained tests contain bounded synthetic instruction vectors and ownership/count/state guards.

All 43,549 original layers passed preparation of the recovered mesh settings. `meshCombine` is enabled in 16,947 layers. Sync masks are 0 in 25,971, 1 in 80, and 8 in 17,498 layers. Mesh divisions are 20 in 22,497, 0 in 20,907, and 9–13 in the remaining 145. No original layer has `meshCombinator`; unsupported combinator behavior remains open.

The player initializer `0x14051a400` sets the global division ratio to float 1.0 at offset `+0x490`. Its owner pointer at `+0x18` initially points to itself; nested players share their owner's pointer. Registration at `0x1402afc63` binds `setMeshDivisionRatio` to `0x1402b2410` and ultimately `0x14051f150`, which updates each owned player. The getter registered at `0x1402afcf2` ultimately reads `+0x490` through `0x14051b460`.

`ScaleMotionMeshDivision` implements truncation after `float(division)*ratio`. `ResolveInheritedMotionMeshDivision` scales the active ancestor's count by `(childWidth+childHeight)/(ancestorWidth+ancestorHeight)` using the original integer operation order, then holds the maximum while the ratio stays equal. `PreparedMotionGeometry::SelectSubdivision` connects that state to the recovered aspect split. All 25,000 count/cache/subdivision outputs matched across 5,000 sequential original requests. Negative/nonfinite ratios, integer overflow, and geometry beyond explicit work limits are rejected; these guards are safety limits rather than claims about native handling of invalid input.

Actual scene resource/parent bindings, visibility overrides, nested motion playback, dynamic follow-mesh offsets, combinators, stencils, and native GPU captures remain acceptance checks. Math coverage does not establish complete animated scene fidelity. The next check is feeding resolved original player state into this evaluator and comparing its emitted geometry and draw commands with native traces.
