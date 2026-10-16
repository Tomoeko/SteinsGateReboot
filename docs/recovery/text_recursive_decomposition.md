# Resident Unicode normalization owner and recursive emission

`UnicodeNormalizationBuffer` owns two arrays of the existing twenty-byte `SubstitutionGlyph` records. It connects output preparation, canonical decomposition, genuine first-party nominal font queries, emission/property refresh, passthrough copying, input consumption, and publication. It is a scoped owner: copying and moving are disabled, and its views borrow that object. Font queries borrow the genuine `resource::SfntFont` and immutable font resource for the duration of each call; the owner does not install callbacks or create a font lifetime.

This is the recovered resident-capacity recursive-emission boundary. It does not implement the complete neutral normalizer or manufacture native allocation failure. Both record arrays are allocated before emission with `MaximumLength + 1` slots, so a request that reaches native growth necessarily exceeds that configured maximum and takes the original maximum-length error branch. No allocation occurs during these operations. Constructor allocation failure remains a normal C++ exception rather than a claimed native heap-failure result.

## Native evidence and representation

Logical target `sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`; Binary Ninja target `candidate:view:1:UEU`. All original code/data and analysis metadata are read-only.

The normalizer context used by `0x14040c630` contains buffer `+8`, font object `+0x10`, Unicode functions `+0x18`, and decomposition procedure `+0x20`. Default wrapper `0x14040cae0` initializes first to the requested codepoint and second to zero, calls the original Unicode decomposition callback `+0x78`, and returns its boolean acceptance. The recovered default data are those in `unicode_decomposition`; no compatibility decomposition, host Unicode normalization, or guessed version is substituted.

The buffer's recovered fields are:

| Offset | Meaning |
| --- | --- |
| `+0x38` | Borrowed Unicode functions |
| `+0x4c` | Sticky scratch flags |
| `+0x50` | Maximum requested record length |
| `+0x80` | Sticky buffer failure |
| `+0x81` | Output phase active |
| `+0x84` | Input cursor |
| `+0x88` | Input record count |
| `+0x8c` | Output record count |
| `+0x90` | Shared resident array capacity |
| `+0x98` | Current input array |
| `+0xa0` | Current output array, possibly aliased to input |
| `+0xa8` | Alternate array |

Begin-output `0x1403a5940` aliases output to input, activates output, and clears only the output count. It preserves cursor, scratch flags, input count, and sticky failure. The immutable native refcount-sentinel profile is outside this owning mutable-buffer boundary.

Preparation `0x1403a5d20` first calls capacity routine `0x1403a5be0` for `output_count + produced`. That routine accepts zero or a request **strictly below** resident capacity before consulting sticky failure or maximum length. A request at/above capacity fails immediately when failure is already set, or sets failure when it exceeds maximum length. General growth advances capacity by `capacity / 2 + 32` until strictly greater than the request, rejects the native arithmetic limit `0x0ccccccc`, and reallocates alternate then input through `0x140583690`. Partial reallocation/pointer publication and allocator-driven failure are still unsupported; the resident profile does not enter those calls.

After successful capacity preparation, aliased output switches to the alternate array only when `output_count + produced > input_cursor + consumed`. It copies the already emitted prefix with the actual original CRT routine. The portable owner copies that same prefix between its separate value arrays. Unused resident slots are value-initialized for safe ownership; active emitted records always originate from validated input records, and the proof starts the native scratch slots in the same state.

## Recursive ordering, emission, and failure

For a successful canonical pair, helper `0x14040c630` queries the second nominal glyph first when second is nonzero. A missing second returns zero before any emission or first query. It then queries the first nominal glyph. `MappedFirst` emits the first and optional second directly when that first mapping succeeds. `RecursiveFirst`, or a missing first mapping, tries the first codepoint's own canonical decomposition. A successful recursion emits its records before the original second; failed recursion falls back to the first nominal mapping when available. Second is never recursively decomposed by this helper.

The helper returns a logical emission count, including when resident emission subsequently fails. The successful caller instruction at `0x14040c80b` advances the input cursor by one whenever that count is nonzero. `TryDecomposeCurrentInput` preserves this caller/helper coupling. A zero result leaves input untouched for the caller's remaining policies; it does not invent a missing-glyph replacement. The complete default first-only canonical graph is acyclic with maximum successful chain length three. The authored depth limit sixteen and resident length limit 65,536 bound work without affecting that recovered graph.

Emitter `0x14040cef0` writes the accepted nominal glyph into the current input record's four-byte word at `+0x0c`, even before an append that fails. At this phase those bits are the nominal-glyph scratch word; the `GlyphProperties`/`PrivateState` field names belong to later uses of the unchanged record layout. Append `0x1403a6350` prepares zero consumed/one produced, copies the complete current input record, substitutes its codepoint at `+0`, and increments output only on success.

After append, the emitter always calls `0x1403f2a30` on the last output record, using index zero when output count is zero. That call refreshes the low character-state word and accumulates scratch flags. Thus a failed append can still change the current input nominal word and refresh an existing output record; with zero output and aliasing, it refreshes input record zero. The implementation uses the already proven `InitializeUnicodeGlyphProperties` operation for this identical record/property update and preserves all inherited mask, cluster, and upper character-state bits.

Passthrough `0x1403f4930` advances only the cursor when output is inactive. With active output and matching aliased cursor/output count, it advances both counts without copying. Otherwise it prepares one consumed/one produced and copies the full record. **A failed preparation returns without advancing the input cursor.** This differs from the recursive helper's logical-count caller behavior and was independently checked with an exhausted mixed-input fixture.

Publication `0x1403a69b0` does nothing while failure is set. Otherwise it clears output-active, swaps input/output pointers when distinct, makes the old input the alternate array, swaps the logical input/output counts even when pointers alias, and resets cursor to zero. The owner preserves the retained previous input and its nominal-word mutations rather than discarding it. A boolean publication result reports whether that transition ran.

## Differential proof and next integration boundary

The unchanged native operations, strict O1 C++20 ASan/UBSan implementation, and independent primitive-table/cmap/buffer model match 373,436 cases and 20,661,925 full record/state words (82,647,700 streamed bytes). Output SHA-256: `8a98aaa27d869ae31722971162443984a2af61c4957c11ae27ae5acca2fb3905`.

The corpus covers every one of 2,060 canonical table entries and 11,172 Hangul syllables under both preferences, four authored font profiles, and all ten streamed original fonts. It also covers singleton/supplementary decomposition, missing-second rejection, failed-deeper-first fallback, limits zero/one/two/three/four/eight, mixed record sequences, cursor preservation across repeated begin, aliased retained-count publication, capacity acceptance before a sticky error, and 470 failure-suppressed publications. Native nominal-query order is checked against the independent model. Four focused sanitizer groups and ten malformed proof requests pass. Compact receipts retain only hashes, native addresses, and bounded synthetic record samples.

The harness calls actual default Unicode callbacks/table tries, actual FT nominal callbacks/cmap classes, recursive helper/emitter/property updater, resident capacity/copy/publication functions, and the original successful caller cursor instruction. Original code, class data, font cmap data, CRT jump tables, and initialized CRT dispatch word `0x140982798 = 1` are read-only. No native allocator or font/Unicode callback is stubbed. Fences, return stacks, pointer domains, unchanged caller context, font/Unicode function state, and all live record words are checked. Fonts and full output streams are not retained. No whole-core library is copied or linked.

The same owner now supports the [neutral first normalization pass](text_neutral_normalization.md), including actual original/decomposition preference, space/nonbreaking fallback, selector handling, cluster backfill and combining reorder. The later [neutral mode-2 recomposition and final glyph publication](text_neutral_recomposition.md) are recovered in the same resident owner. General growth/heap-failure, font/buffer ownership, positions and full shaping remain open. This tranche claims coherent bounded recursive emission, not complete normalization or final-frame fidelity.
