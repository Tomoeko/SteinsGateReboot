# Prepared CFF hint maps

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses below are executable virtual addresses.

`cff_hint_map` implements the native CF2 prepared-edge evaluator, insertion and pixel snapping. It consumes already constructed character-space/device-space 16.16 edges; it does not construct blue zones, process hint masks, execute the hinted charstring loader or rasterize glyphs.

## Recovered operations

- `0x1403c7950` evaluates a cached segment. Disabled/empty maps multiply by the base scale without changing the cursor. Before the first knot it uses the base scale anchored to that knot; elsewhere it uses the selected edge's segment scale. Knot equality advances the cursor. Signed fixed multiplication is native `0x14039cda0`, with negative half values rounded away from zero.
- `0x1403c7760` inserts one edge or a stem pair into ordered fixed storage. It rejects duplicate character positions, overlap with a following stem pair, reversed pairs, device-order violations and capacity exhaustion. An initialized parent remaps an unlocked candidate or the pair's midpoint; midpoint and half-width truncate toward zero before scaling. Device-order/capacity rejection occurs after that remap, so the returned candidate retains its changed position. At most 192 edges fit; a final single edge may fill the 192nd slot, while a pair needs two available slots. Insertion does not recompute segment scales or set initialization state.
- `0x1403c6bf0` translates an unlocked edge/pair toward an integral device pixel while retaining half-pixel separation from neighbors. Equally near choices prefer upward. It recomputes segment slopes using signed rounded fixed division, then replays deferred upward alternatives in reverse order. Native late adjustments do **not** recompute those slopes. Locked edges still participate in slope updates; coincident character positions retain the prior slope.

Native map fields are parent pointer `+8`, pending storage `+0x10`, initialized byte `+0x18`, enabled byte `+0x19`, base scale `+0x1c`, count `+0x20`, cursor `+0x24`, and 32-byte edges beginning `+0x28`. Edge flags are lower ghost 1, upper ghost 2, lower stem 4, upper stem 8, locked 16 and synthetic 32. `0x1403c6ec0` establishes those kinds from ghost widths −21/−20, ordinary stem sides, cached positions and synthetic boundaries. The public values represent those fields explicitly and never serialize a native structure.

The native advance getter `0x14039af80` ORs internal advance-only bit `0x100` before glyph loading at `0x14039b0cf..0x14039b0e9`. Loader `0x1403cc8bb..0x1403cc8c9` transfers that bit into decoder `+0x331`; stem declaration `0x1403c59b0` skips collection in that mode. Original VectorFont outline flag8 requests no bitmap; it does not set advance-only. The map builder's initialized byte store at `0x1403c772c` must not be reconstructed as a wide write that would clear the adjacent enabled byte.

## Validation and boundaries

Unchanged executable helpers ran against preallocated private CPU storage without imported allocators, callback substitutions or a game launch. Independent portable models and authored C++ matched:

- 1,000 maps /16,000 sequential evaluations: 32,000 mapped-value/cursor words, SHA-256 `d98f68a2c5f365c13aa5d6a1142839406e879521176fdbd0597c5bdf84ecb97d`.
- 1,000 snapping fixtures: 2,133 deferred alternatives, 96 applied late alternatives, SHA-256 `ab296e49e4d0a7ab677c9831ec602e86be9bacb421017bd7fa958994e063a897`.
- 2,000 insertion fixtures: 453 accepted insertions, 244 candidate remaps, SHA-256 `36a838fcaa8629ed6e3cdfbe450e9d85d67c8d1c00cbcb9b573e7bb779b1a8e0`.

The combined C++ differential matched 349,091 meaningful edge/coordinate/cursor values. Padding bytes and foreign pointers are excluded. Focused tests retain negative rounding, boundary/cursor transitions, capacity, pre-rejection mutation, paired snapping, and late-adjustment slope ordering. Strict warnings, ASAN/UBSAN and project formatting pass.

The authored boundary requires positive base scales, recognized edge kinds, bounded count/cursor, ordered character positions and complete lower/upper pairs for snapping. Coordinate arithmetic exceeding signed 16.16 storage is rejected. Segment fixed division retains the native low 32 quotient bits, including a one-unit language-1 em-box interval whose slope wraps; explicit unsigned operations avoid C++ signed overflow. Native blue-zone generation/locking, active masks and width declaration remain separate recovery work; these helper proofs do not establish hinted outline or pixel fidelity.

Counter-mask integration added 1,000 native coupled owners: all 118,380 map/mask/cache values match, with 3,196 cache updates and unchanged primary masks. Native quotient boundary examples at `0x14039aa80` are `32768/1 → 0x80000000` and `65536/1 → 0`; focused locked-edge tests preserve these segment slopes. The existing 349,091 helper values still match after this correction. See [Type2 masks](cff_charstring_mask.md).
