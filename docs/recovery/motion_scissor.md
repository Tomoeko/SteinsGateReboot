# Motion scissor recovery

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses refer to the explicitly selected original PE Binary Ninja view. Original bytes and analysis metadata remain unchanged.

## Affine rectangle transformation

Concrete `MMotionLayer` render vtable `0x140652d80` slot `+0x88` forwards through `0x140559d40` to graphics helper `0x1404c2a70`. It composes two row-major affine 3×4 matrices through `0x1405308f0` and transforms four XY corners at Z=0 with `0x140530e00`; there is no perspective divide. Corner order is minimum/minimum, maximum/minimum, maximum/maximum, minimum/maximum. MINSS/MAXSS preserve the second operand on equal or unordered comparisons.

Physical screen X is `float(width) * transformed_x * 0.5f + float(width / 2)`. Screen Y is `-float(height / 2) * transformed_y + float(height / 2)`. Both integer halves truncate before conversion. Odd dimensions therefore cannot be replaced with symmetric floating-point half extents. The authored transform preserves each original float32 operation with contraction disabled. It accepts finite matrices/rectangles and positive physical dimensions, rejecting nonfinite intermediate/output values; these are authored scope checks. Rectangles may arrive reversed because the native helper still measures all four corners.

## Cache and submission

Native setter `0x1404c5de0` stores raw requested floats at graphics `+0xb8..+0xc4`, then rounds them through bit-based float helper `0x1405823e0` into `+0xc8..+0xd4`. Halfway values round away from zero; negative small values retain negative zero. It compares that rounded tuple against the last submitted rounded cache `+0xd8..+0xe4`. Ordered signed zeros compare equal. Equal infinities suppress submission; any NaN component forces it even when both words match.

When the rounded tuple changes, the original submits integers produced by CVTTSS2SI truncation of the raw requested floats, then records the rounded tuple after its device callback returns. Invalid or out-of-range conversions produce the integer-indefinite word `0x80000000`. Thus raw values 1.9 and 2.1 both cache as 2, even though their submitted integers would be 1 and 2: the second update is suppressed. This recovered suppression is preserved rather than silently changing cache keys.

`PrepareMotionScissorUpdate` returns owned raw/rounded values plus an optional integer submission. The caller stores raw/rounded state before submission and updates the applied rounded cache only after successful submission. It receives the existing cache explicitly; the original graphics-owner initialization is not inferred. The helper invokes no graphics, OS, input, or audio service.

## Validation and remaining work

Independent ASan/UBSan fixtures cover four corners, rotation, reversed bounds, odd viewports, affine translation order, preflight failures, raw truncation versus rounded suppression, retry with unchanged applied cache, ties, signed zeros, infinity/NaN payloads, and integer boundaries.

Two thousand bounded cases execute unchanged original code: 1,000 finite affine transforms with varied matrix coefficients/odd dimensions and 1,000 raw scissor/cache states with finite values, signed zeros, infinities, quiet/signaling NaNs and conversion boundaries. All transform words, raw/rounded/cached words, submission decisions and integer packets match. The 561,530-instruction trace SHA-256 is `0daf02206aa17e723a17ffb688f7e6d4746219b96d6b0feeb5c7f4950365c1eb`. Only the private buffer-only device callback is substituted; no OS or GPU call executes. Compact vectors/report remain ignored under `Files/research/runtime/`.

Floating-point exception flags, nonfinite rectangle transformation, original graphics initialization, automatic clip-group floor/ceil expansion, clip policy/stencil phases, actual Direct3D rectangle acceptance and Metal pixel output remain open. This numeric/state boundary does not establish full motion clipping fidelity.
