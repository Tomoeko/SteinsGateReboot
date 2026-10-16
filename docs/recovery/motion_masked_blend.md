# Motion masked blend passes

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses refer to the explicitly selected original PE Binary Ninja view. Original bytes and analysis metadata remain unchanged.

## Recovered pass selection

Original queue submission `0x140555610` routes special blend 0x10d through `0x140557910`. The helper obtains current target bounds through render-owner virtual slot +0x90 and axis scales through +0x98. The concrete owner supplies centered target bounds `0x1404c5cb0` and unit scales `0x14052acc0`; unlike scissor mapping, centered bounds multiply the full unsigned target dimensions by float32 0.5 rather than using integer halves. The general helper also contains an unequal-scale branch.

With equal scales, it computes scaled width and height, selects the larger using the original MAXSS operand order, multiplies by the amount, adds float32 0.5 and truncates to a signed 32-bit integer. Positive radii select the largest fitting step from original table `0x140655b80`: 1, 2, 3, 6, 9. Each selection emits a horizontal pass followed by a vertical pass, then restarts the search at 9. Radius 18 therefore selects 9/9 pairs twice.

With unequal scales, each radius is calculated in the different native operation order `(dimension * amount) * axisScale + 0.5`. All horizontal passes precede all vertical passes. Each scan visits 9, 6, 3, 2, 1 once, subtracting every fitting step before restarting. Radius 18 therefore selects 9, 6, 3; radius 22 selects 9, 6, 3, 2, 1, then 1. Horizontal blend parameters are the exact integral floats 0–4 for ascending step-table indices; vertical parameters are 5–9. Nonpositive integer radii emit no passes. CVTTSS2SI yields INT_MIN on invalid/out-of-range intermediates, including overflow from finite inputs, which also emits no passes.

Each selected pass flushes the previous batch, selects blend mode/parameter, obtains the command's lazy geometry bounds, flushes again, supplies those bounds through slot +0x68, and appends the command geometry through `0x140556c20`. The helper leaves the last appended batch pending. Geometry and surrounding state orchestration remain caller-owned.

`PrepareMotionMaskedBlendPasses` writes axis, step and blend parameter into caller-prepared storage without allocation or hardware calls. It rejects nonfinite input and preflights capacity before writing. Analytic complete-cycle counting bounds preflight work for very large valid radii; at most 65,536 passes may be prepared. That limit and explicit nonfinite rejection are authored safety rules. Reversed bounds and negative finite scales/amounts retain native arithmetic rather than being normalized or clamped. Native floating-point operation order requires contraction disabled; nondefault floating-point environments remain unverified.

## Validation and remaining work

One thousand bounded native cases execute 344,388 unchanged original instructions, including radius conversion, paired/separate-axis scheduling, step lookup, lazy geometry bounds and empty batch flushes. The authored planner matches all 3,216 pass parameters and orders with zero differences. Every native pass has the measured flush/blend/flush/bounds/append order, and geometry-cache initialization occurs only when a pass is selected. Trace SHA-256: `2a0788ec3c7757d21abe8c9d7ca506a48e0951f0c73ebd4735425c8e03dfb561`.

Target/scales callbacks provide private prepared values. Downstream quad append is replaced by a private event observer; its implementation is validated separately by the ordinary quad tranche. No GPU/OS callback, game launch, speaker output or original patch occurs. Independent strict ASan/UBSan fixtures cover paired versus separate-axis decomposition, lower-step scan order, complete scans, float32 half-threshold rounding, signed-zero scales, invalid conversion intermediates and preflight capacity/work failures without partial writes. Compact synthetic vectors and reports remain ignored under `Files/research/runtime/`.

Shader/kernel interpretation, target ping-pong/texture state, complete blend/geometry/batch orchestration, other special blend modes and native/Metal pixel equivalence remain open. Pass selection alone does not establish filtered image fidelity.
