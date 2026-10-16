# Initial dotted-circle emission

`UnicodeNormalizationBuffer::InsertInitialDottedCircle` recovers the actual `0x1403f2fa0` phase on the existing scoped resident owner. The shaping caller invokes it at `0x1403f3e98`, after Unicode property initialization and before initial cluster grouping or direction adjustment. The method borrows the same immutable `SfntFont` used by the later neutral caller; it does not create a platform font or replace missing glyphs.

The logical reference is `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. All analysis explicitly selected `candidate:view:1:UEU`; all executed instructions and tables were read from this unmodified executable. This source extends the frozen neutral caller header without changing any record, owner or state fields. Existing normalization source remains unchanged.

## Caller inputs and order

The explicit `InitialDottedCircleContext` carries the original text flags word at buffer `+0x40` and preceding-context length at `+0xdc`. Only bit 0 of the flags affects this phase; other bits are preserved as caller inputs. The preceding-context count is derived directly from `0x1403a5630`: when UTF-16 input begins at a nonzero item offset and the buffer initially has no records, `0x1403a56e8..0x1403a576d` scans up to five decoded preceding codepoints backwards into `+0xb4` and increments `+0xdc`. The separate following-context count lives at `+0xe0`. No host Unicode decoding or guessed flag interpretation is used by this phase.

The native gates are:

1. Text flags bit 0 is set.
2. Preceding-context length equals zero.
3. The low five bits of the first input's supplied `CharacterState` equal 12.
4. The actual nominal callback accepts U+25CC.

The category gate reads the first record, regardless of the current input cursor. It does not reclassify that input codepoint. Its nominal query is only an availability check: the returned glyph index is discarded. A zero-initialized 20-byte record is created with `GlyphIndex` U+25CC, and its Unicode properties are refreshed by `0x1403f2a30` before starting output. The nominal scratch word at `+0x0c` stays zero.

Then `0x1403a5940` starts output and the input cursor is explicitly reset to zero. The circle receives the first input's complete mask and cluster; other state words remain those of the new initialized record. `0x1403a63d0` requests a zero-input/one-output append and writes all five words only on success. The caller copies every original record until the input ends or its sticky error is set, preserving all mask/cluster/private/character words. It attempts publication through `0x1403a69b0` unconditionally. A failed owner suppresses publication and retains the partially written arrays, counts, cursor and active/alias state.

The resident check runs before the sticky-error check. Consequently, a previously failed owner can still append a circle when the request fits its resident capacity; it then skips the copy loop and suppresses publication. This is preserved instead of replacing partial output with an error fallback.

The native helper reads the first record without a live-count check. The authored method requires a nonempty live input when its text/context gates permit that read and reports a precise precondition error otherwise. Disabled or preceding-context-gated empty input remains unchanged. The enclosing original shaping entry and general buffer construction remain a separate recovery prerequisite.

## Bounds and lifetime

The owner still preallocates two `MaximumLength + 1` initialized arrays and retains the existing 65,536-record bound. During append/copy no allocation occurs. Requests equal to resident capacity fail with the original sticky-error and publication order; requests strictly below capacity fit even if the owner already failed. Allocator-driven growth, heap failure and generic mutable buffer lifetime remain open. The borrowed font's immutable bytes must outlive the call; views of record arrays borrow their scoped owner.

## Native validation

Three strict C++20 fixture groups built with ordinary `-O1`, conversion/sign warnings as errors, ASan and UBSan. They cover property-before-output order, availability-only nominal querying, supplied-category gates, first-record mask/unsigned cluster copying, preexisting output/cursor restart, zero nominal word, repeated insertion, resident failure/partial publication, fitting appends after sticky failure, and the live-first-record precondition.

The independent native harness executed the complete original `0x1403f2fa0..0x1403f315c`, append/copy/publication and default Unicode/nominal callbacks, including the original security-cookie check. Code and tables remain read-only; caller-owned arrays are fenced and font/callback objects are checked unchanged. Four authored valid SFNT fixtures exercise absent U+25CC, accepted U+25CC, a zero map, and the last valid glyph. All ten original font resources are streamed one at a time and discarded after comparison. No original font, full output stream or raw record table is retained.

All **25,592 cases and 885,570 state/record words** match original execution, first-party C++ and an independent model decoded from the original Unicode/cmap data. The corpus includes **4,280 accepted emission attempts**, **1,200 failed-publication cases** and **6,500 nominal callback queries**. Nominal-query and properties/begin/append/publication order independently match. Native/C++ and independent-model stream SHA-256 both equal `22d64ef755f7c80e65a7b90485f4858de30d53fe3de9a9fd7ae4db77b04b0629`. The final executable additionally passes **108 compact native snapshots** and **17 malformed driver/parser cases**. Invalid format12 glyph indices remain explicit errors from the genuine font reader, rather than silent nominal fallback.

## Earlier face cache and later GDEF prerequisites

The earlier neutral fallback decision at `0x1403f3de4..0x1403f3e3d` queries an effective face-layout cache through `0x14040a820` and `0x14040a7e0`. The actual neutral descriptor has no `+0x50` override. For an already-ready cache, fallback mark positioning is enabled iff effective GPOS data is absent; fallback glyph classification is enabled iff the effective GDEF ClassDef offset is zero. A sticky-failed cache enables both. These are derived values, not arbitrary user preferences.

The cache constructor `0x1403fe9c0` owns sanitized immutable GDEF/GSUB/GPOS blobs at `+0/+8/+0x10`, borrows their effective table data at `+0x18/+0x20/+0x28`, borrows the face for lazy auxiliaries, and owns two arrays of 12-byte lookup accelerators. A material exception policy uses the **sanitized blob lengths**: 28 exact GDEF/GSUB/GPOS length triples unconditionally replace the effective GDEF data pointer with the null table, and six further triples do so only if glyph 5 has GDEF class 3. The owned GDEF blob remains retained. Therefore raw `SfntFont::FindTable("GDEF")` or an arbitrary unsanitized class reader cannot faithfully supply the effective fallback/GDEF decision.

Private `face-cache-prerequisite.json` retains all 34 exact triples, the original decision-slice hashes, and an independent native check of **3,754 triple/class/boundary/random cases**. Its decision-stream SHA-256 is `fe1844d2db7360e3e972507b524b3d68f2d4d5ab1a4a5da7b5eaa462b3408470`. All eight ready/failed-cache, GPOS-presence and ClassDef-presence combinations independently execute the original neutral fallback branch. This receipt is analysis evidence; no cache constructor, sanitizer or allocator was replaced or claimed implemented.

Face `+0x68` is null before preparation, `-1` after sticky preparation failure, or a cache pointer. `0x1403f4d30` constructs on demand and discards a redundant successful candidate through `0x1403ff200` if an existing cache appears before publication. The destructor frees both accelerator arrays, the three owned blobs and three lazy auxiliary blobs, then the cache. Exact blob sanitization/repair, provider ownership, allocation failure and publication/reentrancy still need native proof before connecting this lifecycle to the existing first-party font/feature owners.

After the now-recovered neutral caller's final content-tag-2 mapping, `0x140404010` reads that same effective GDEF data and replaces each record's `+0x0c` properties while clearing `+0x0e/+0x0f`. Classes 1/2 become properties 2/4; class 3 becomes `8 | (MarkAttachClass << 8)` narrowed to 16 bits; other classes become zero. The other four record words remain unchanged. Existing first-party layout readers provide these query primitives, but the effective sanitized/exception-policy owner remains the prerequisite for a faithful integrated GDEF entry.

Next: recover the genuine table-provider/blob sanitizer and lazy cache publication, then derive the neutral caller fallback flag from that prepared owner and run GDEF entry before GSUB. Full lookup application, positions, complex-script descriptors, allocator-driven growth and final text-frame fidelity remain open.

The coherent public sanitizer executable passes108 retained native snapshots and17 malformed-driver requests. Integrated and portable Release fixtures pass at300/275; complete general-buffer/cache ownership and final text-frame fidelity remain open.
