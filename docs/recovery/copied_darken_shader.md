# Copied-destination darken

Actual resource entry 117 reaches raw blend `0x118`, zero blend parameter. Native `0x140532140` dispatches family `0x100` through `0x14054d8a0`; operation `(mode & 0xff0f) - 0x100 + 1` is 9. This preserves the real per-row copied bounds, sampled copied destination at `t2/s2`, and screen UV affine in `cb0`. Native `0x1405316c9` selects blend row 10 when enabled, row 7 when disabled: RGB replaces the target, while enabled row 10 preserves its alpha.

The original getters `0x140566440/450` were independently executed against initialized original PE data for all 2,089 shader containers. The compact index trace SHA-256 is `4a063fbd86b44ba52169cfc0f6e5b931689dbc84b778b7dbfabe2f7f13bbe02f`. The reference executable SHA-256 is `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

| Runtime pixel program | File offset | Original SHA-256 |
| --- | --- | --- |
| 1137, ignore vertex color | `0x8691d0` | `40704f0c9694aaedd3d0f8b6477816bc6281bb9770fe02a1f606a34121341c02` |
| 1289, multiply vertex color and double RGB | `0x8a0460` | `8471ab7780cb0054b7cf84a45b8964058ce4608180a8dc9ea35bdab00e2ecee7` |
| 1156, masked 1137 | `0x86fa10` | `6b4b235a40caf9a8c82d92a8854a699ddc0b3f6dad41961a4079aa6c23b57dad` |
| 1308, masked 1289 | `0x8a7220` | `3246e897cc12a12b239cfb0d1d7e29ef851529baf5def159491f189936a8cfcc` |

Each source color is optionally multiplied by the actual vertex color and `(2,2,2,1)`. The shader selects per-channel `min(source.rgb, copied.rgb)`, subtracts copied RGB, then adds the opacity-weighted difference to copied RGB. The unmasked opacity is the resulting source alpha. The masked programs sample the actual screen mask at `t1/s1`, multiply its alpha after source modulation, and keep screen reciprocal in `cb0[0].xy` and copied affine in `cb0[1]`. Copied destination alpha is unused.

The narrow receiving change admits exactly `0x118` RGBA/Bc7 source interpretation with actual attribute bits, retaining ordinary/copy/mask lifecycle guards. It does not admit `0x108`, other copied operations, color filters, auxiliary target layouts or non-RGBA copied sources. The independent alternate-selector corpus already compares 1,920 original cases / 5,760 calls; readable HLSL complete-container comparison and Metal pixel results remain separate measurements. No cross-API bit identity is inferred from source equivalence.

## Connected validation

All four readable HLSL programs reproduce their complete original DXBC containers with the retained compiler. The actual Metal receiver passes copied/masked modulation and rebind checks with28 prepared typed pipeline outputs. A failed fixture supplied ARGB to a documented RRGGBBAA frame input; correcting that stimulus preserved the expected pixel and tolerance. A separate stale26-entry receiver bound was corrected to28 while retaining exact table equality and typed-output checks.

The original resource117 now completes six actual containing update/draw/CPU-submission phases with mode0x118. Whole-scene GPU/native image and cross-API timing/floating-point parity remain unaccepted.
