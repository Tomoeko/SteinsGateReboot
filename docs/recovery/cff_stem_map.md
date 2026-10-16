# Prepared CFF stem maps

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are executable virtual addresses.

`cff_stem_map` reproduces prepared horizontal-stem, blue-zone and active-mask linkage from CF2 map builder `0x1403c6ec0`. It consumes declared stem endpoints and quantized blue zones; it does not execute a charstring, infer mask timing or emit a hinted contour.

## Native order and ownership

The native ABI has six arguments: current map, horizontal vector, vertical vector, mask, vertical origin and initial-map flag. At entry, RBP is RSP−0x4f; origin reads from RBP+0x77 (entry RSP+0x28), and the flag from RBP+0x7f (entry RSP+0x30). Decompiler argument numbering includes an extra unused argument and is unsuitable for a callable prototype.

An uninitialized parent is first built from all declared hint bits. Missing masks become all-active, bounded to 96 horizontal+vertical declarations with MSB-first bytes. Vertical declarations contribute count/bits to this recovered Y-axis construction; only the horizontal vector contributes edges. The original resets count and cursor together at `0x1403c6fd1`, preserves the enabled byte and sets the initialized byte at `0x1403c772c`. Successful construction consumes the mask's new flag at `0x1403c7731`.

Stem width −21 supplies a lower ghost at its declared end; width −20 supplies an upper ghost at its declared start. Other negative widths reorder the geometric lower/upper sides. Upper sides include twice vertical darkening, and both sides include the supplied vertical origin. Cached active sides become locked and use their corresponding saved device coordinate.

The first pass inserts cached or blue-locked selected stems, in declaration order. A bottom zone matches a lower edge and a top zone an upper edge within inclusive fuzz bounds. Suppression uses the zone's flat device coordinate. Otherwise BlueShift determines ordinary pixel rounding or an outward minimum-one-pixel overshoot. The offset translates both active sides and locks them. A selected locked bit is cleared from the local remaining-mask copy even when geometric insertion rejects its edges.

Initial maps retain only that first pass plus any em-box edges. If their character range does not contain zero, the native adds a synthetic locked zero anchor. Current maps then insert the remaining unlocked selected stems through the initialized parent's mapping, apply native snapping/deferred adjustment order, and cache non-synthetic mapped sides. Ghosts update only the active cache side. Rejected duplicates do not acquire fabricated cache entries.

The initial map and stem cache belong to one glyph-load invocation and persist across its masks. The caller starts fresh state for another glyph/size/matrix. Input spans borrow caller-owned mutable stem storage; maps, masks and numeric results own their fixed storage. Synthetic edges have no glyph stem identity and never update that cache. No allocation occurs inside construction.

## Verification and remaining work

Unchanged native blue-zone and map builders ran in preallocated private CPU storage, without imported allocation, substituted callbacks, GPU work or a game launch. One thousand fixture glyph owners each received four sequential masks: 4,000 builds, 6,132 cache updates and a maximum 29 edges. Authored C++ matched all 472,410 meaningful current/initial map, mask and stem-cache words. Native result SHA-256 is `3a4e1bb47051a3562f1d575e8222913d06d547a8b34b244dc45f64bf1f85203c`.

Focused tests retain all-active and vertical-only masks, cache locking across masks, ghost/ordinary negative widths, top-side darkening, blue overshoot thresholds, duplicate rejection and mask/count/owner bounds. Project warnings including conversion/sign conversion, ASAN/UBSAN and formatting pass. The authored boundary rejects incompatible mask counts, excessive declarations, invalid prepared configuration, same-owner current/initial maps and signed fixed-point overflow.

Actual Type2 declaration/mask execution, path continuity across map changes, control-point hint transformation, native private-real quantization and final hinted/raster pixels remain separate acceptance checks. The prepared-state proof covers this construction stage only.
