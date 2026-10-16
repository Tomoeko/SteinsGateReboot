# Neutral Unicode segment normalization before recomposition

This stage extends the scoped `UnicodeNormalizationBuffer` with `NormalizeNeutralBeforeRecomposition`. It connects the recovered nominal/variation queries and canonical decomposition to the actual neutral segment walk, resident two-input emission, cluster propagation, publication and combining reorder. It stops at the original recomposition boundary; codepoints remain in `SubstitutionGlyph::GlyphIndex`, while the complete four-byte word at `+0x0c` holds nominal or variation glyph indices. The class keeps the existing record arrays and state layout.

Evidence is the supplied executable `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`, image base `0x140000000`. Binary Ninja reads explicitly selected `candidate:view:1:UEU`. No binary bytes or analysis metadata were modified. Addresses below are virtual addresses. Algorithm names and ownership are first-party reconstructions; native instruction execution and byte observations establish behavior.

The neutral descriptor at `0x140623700` is 104 bytes, SHA-256 `59d8d1d5dee371edeb516afb2f26e0054e36d895ce70a2e42a11f064df90abba`. Its mode word at `+0x30` is 2; decomposition, composition and post-reorder overrides at `+0x38`, `+0x40` and `+0x58` are null. Only this actual default neutral profile is implemented.

## Segment and mapping order

The recovered first pass runs `0x14040bf50` through the boundary at `0x14040c1bd`. Empty input returns without entering output mode. Otherwise it begins output, sets the input cursor to zero and retains the original input count. Each segment includes its first record and every following record whose **supplied** category, low five bits of `CharacterState`, is 10, 11 or 12. An isolated record uses mapped-first `0x14040c770`: original nominal mapping wins; canonical decomposition is attempted only if that mapping fails. A multirecord segment without a recognized selector uses recursive-first mapping. A segment containing a recognized selector routes directly through `0x14040cb10`; that entire segment uses nominal/variation queries without canonical or space fallback.

Properties must already be initialized by the recovered preprocessing phase. This method deliberately retains the supplied state for unchanged codepoints: reclassification here would change native behavior after preceding phases. Newly emitted decomposed codepoints use the already recovered property callback. The genuine `SfntFont` and its immutable resource-byte owner must stay alive throughout the call. A prepared local `VectorFontVariationMap` borrows that same font; no replacement font or fabricated font owner exists.

All mapping failures retain the original codepoint and use glyph index zero unless an actual native fallback succeeds. Space fallback requires supplied category 29, one of the following codepoints and successful nominal mapping of U+0020. It preserves the low property byte and upper sixteen bits, replaces the property's high byte with the marker, sets the nominal scratch word, copies the record, then ORs scratch bit 4 even if copying failed.

| Codepoints | Marker in property high byte |
| --- | --- |
| U+0020, U+00A0 | 18 |
| U+2000, U+2002 | 2 |
| U+2001, U+2003, U+3000 | 1 |
| U+2004 | 3 |
| U+2005 | 4 |
| U+2006 | 6 |
| U+2007 | 19 |
| U+2008 | 20 |
| U+2009 | 5 |
| U+200A | 16 |
| U+202F | 21 |
| U+205F | 17 |

U+1680 has no fallback marker. Missing U+2011 queries U+2010; a successful alternate glyph retains U+2011 and its supplied properties. These are direct `0x14040c770` instruction/jump-table conclusions, not host Unicode or font policies.

## Selectors, resident emission and clusters

Selectors are exactly U+FE00–U+FE0F and U+E0100–U+E01EF. `0x14040cb10` looks at the next codepoint in its segment. A recognized pair first zeros the base record's nominal word and calls the actual variation callback. Success writes its glyph index, then `0x1403a6520` prepares consumption of two inputs and production of one output. Only successful preparation merges the input clusters and copies the complete base record. The base codepoint, mask and character state remain intact; cursor advances by two and output count by one. An explicit format14 glyph index above the face glyph count remains valid because the original variation path does not apply nominal clipping.

A failed variation query maps and copies the base, then maps and copies the record at the resulting cursor, including the native case where the first failed copy did not advance. Additional consecutive selectors are individually mapped and copied. A terminal record is nominally mapped and copied even if the segment's preceding operation set the sticky error. Missing selectors remain records; they are not silently discarded.

Cluster merge is the actual `0x1403a5db0`/`0x1403a6b50` behavior. Levels 0 and 1 use the minimum unsigned cluster, extend equal-cluster boundaries, stop backward extension at the current input cursor and backfill matching previously emitted output records when the merge begins at that cursor. Changed clusters clear mask flag `0x1`. Level 2 retains clusters and marks nonminimum records unsafe by ORing mask flag `0x1` and scratch bit `0x10`. Preparing two-to-one emission precedes cluster mutation. The output tail is inspected against the current input boundary on each iteration, including aliasing.

The normalizer calls publication even after a sticky error. Failed publication preserves active output, counts, pointer identities and the current input cursor. Combining reorder still runs on the original input. Its cluster helper therefore uses the **actual current cursor**, which may be nonzero; calling the existing standalone reorder interface, whose proven profile starts at cursor zero, would lose this failure behavior. Stable insertion ordering is limited to runs of 2–32 records with a nonzero modified combining class and category 10–12. Longer runs remain unchanged. The next separator after a run is skipped exactly as in the native scan.

## Bounded unsupported edge and remaining work

The owner preallocates both arrays with `MaximumLength + 1` slots and retains the earlier native resident-capacity and maximum-length semantics. This orchestration is bounded to 4096 live input records and maximum length 4096, with cluster levels 0–2. These are explicit authored work limits, not inferred native format limits.

There is a directly observed nonterminating native edge: when an extra selector's copy cannot fit after prior expansion, `0x14040ce44` retains its cursor and loops through the same selector query and failed preparation again. The portable method reports `neutral Unicode extra-selector copy cannot advance after resident capacity failure` at the matching state instead of claiming publication or consuming the selector. Native proof stops at that checkpoint, repeats one complete loop iteration and verifies identical state plus the repeated query. This is explicitly unsupported noncompletion, not a fidelity claim that native errors recover.

Allocator-driven growth/reallocation and allocation failure remain unimplemented. The later [mode-2 recomposition and separate final glyph publication](text_neutral_recomposition.md) are validated in the same resident domain. Other normalizer descriptors/modes, dotted-circle insertion and complete shaping ownership remain open. This stage does not use a guessed Unicode version, host normalization, platform font fallback or a copied normalization implementation.

## Validation

The isolated C++20 proof uses `-O1`, strict conversion/sign warnings as errors, ASan and UBSan. Five fixture groups cover segment preference/order, original and space/nonbreaking policy, selectors and prior-output cluster backfill, failure/nonprogress and empty/bounded-work behavior. The native harness executes the original first-pass instructions, nominal/variation font services, recursive property callbacks, resident publication, merge and sorting on bounded caller-owned memory with array fences and immutable font/context checks. Complete owner states and all five words of both live input/output views are compared to C++ and an independent model decoded from the original Unicode and cmap tables. Nominal/variation callback order is independently compared.

The corpus includes all 2060 canonical table keys in both isolated and combining contexts, Hangul boundaries and every LV syllable, all recognized selector values and their neighbors, each original format14 default-range and explicit-map boundary, category-gated spaces, levels 0–2, repeated/unsigned clusters, bounded random segments, failure-suppressed publication and the native 32-record combining-sort boundary. Four authored metadata/cmap fixtures and all ten original font resources are read one at a time; no original font or full differential output is retained. The complete proof matched 139,806 cases and 4,295,955 words (17,183,820 streamed output bytes), with 77,523 variation callback queries, 632 failure-suppressed publications and 120 repeated-state nonprogress checkpoints. Native/C++ and independent-model stream SHA-256 both equal `f389192508f3e8193d4c070ccb51a9a01601b4ed428860d9ec775a681f0a6ce9`. The final executable also passed 76 compact native replays and 11 malformed-driver cases.
