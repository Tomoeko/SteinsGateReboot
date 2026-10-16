# Direct single substitution records

`ApplyDirectSingleSubstitution` borrows initialized glyph records and applies one actual flags-zero GSUB type-one lookup in place. Its records preserve glyph index, mask, cluster, packed GDEF properties and the two remaining state fields. The state at original offsets `+0xe/+0xf/+0x10` stays opaque; this operation preserves it. It allocates no buffers.

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are original virtual addresses. Binary Ninja inspection explicitly targets the PE view. No original bytes or analysis metadata are changed.

The original ordinary forward loop at `0x1404019b0` first checks the candidate digest, then the intersection between the lookup and glyph masks, then lookup flags through `0x1404025b0`. Under flags zero, the glyph-class filter accepts the record. The first covering subtable applies; later subtables are skipped. Format-one and format-two single operations are `0x140401680/401720`, with coverage indexing at `0x140403100`.

For an applied replacement, `0x140405560` updates the low property word only when the actual GDEF glyph-class offset is nonzero. It preserves bits `0x60`, ORs the replacement's packed GDEF properties and bit `0x10`. The record helper `0x1403a6460` replaces the glyph index and advances input/output cursors. It preserves mask, cluster, the high half of the property word and the remaining auxiliary word. A covered identity replacement still reports an application. Absent GDEF classes preserve the existing property word.

The original forward single-substitution operation begins with aliased input/output records and never expands them. This module implements that in-place scope. Native buffer construction, other output-buffer states, insertion/deletion, cluster merging, contextual matching, manual-joiner policy and reverse application remain separate work. Numeric schedule options are retained by the plan; this direct-single path does not consult them.

Input/replacement preflight checks the entire operation before publishing malformed-input changes. Work is bounded to 65,536 glyphs. Invalid glyph/lookup references, non-GSUB tables, nonzero flags, other lookup types and feature variations fail explicitly. The portable safety rejection for malformed input is not an assertion about native malformed-font behavior.

Three independent sanitizer groups cover mask intersection, property updates, preservation of every other record field, missing GDEF, zero masks, covered identity replacements, bounds, unsupported filters/table identity and preflight without partial publication.

A genuine unchanged-original comparison runs `0x1404019b0` and the single/coverage/GDEF/record helpers against 512 synthetic configurations. It matches 8,136 glyphs and 41,192 words across 1,583,276 original instructions; output SHA-256 `8110b41d7a44afae649e2a8a92598713d02b2d91f835a13b00b5c9f0548b174c`. Fixtures cover both coverage and single formats, multiple subtables, negative/zero/positive deltas, wide attachment values and masked/uncovered records. Native cursor/count and aliased-pointer invariants are also checked.

A second comparison uses actual sampled flags-zero direct-single payloads from every original font: 120 cases, 3,840 glyphs and 19,320 words across 501,146 original instructions. It includes covered/uncovered candidates and zero/nonzero masks. Output SHA-256 `2cf3e320859c6364305e6aea676ebf0d089fc0e43ce54c908e0e141cd034cf77`.

Caller-owned buffers and actual dispatch entries are preallocated. The candidate digest has all bits set, deliberately allowing false positives that the genuine coverage helper rejects. No original helper is replaced, and no allocation, OS, GPU or initialized game callback executes. Compact synthetic samples and regeneration source remain ignored. Original font sampling is streamed without retained font copies.

This is a verified direct-single operation, not a complete shaper. TextRender integration, Unicode preprocessing, filtered/contextual substitution, multiple/ligature/reverse operations, GPOS, fallback kerning, glyph positions, complete measurement and final text/frame parity remain open.
