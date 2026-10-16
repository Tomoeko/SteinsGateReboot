# Original initial Unicode clustering

`InitializeUnicodeGlyphClusters` recovers the in-place initial grouping phase after Unicode property preparation and before direction adjustment or glyph mapping. It consumes prepared `SubstitutionGlyph` records and the existing scratch flags, with input cursor zero and output count zero. This is a complete phase for that caller state; dotted-circle insertion and general buffer/output ownership remain open.

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are original virtual addresses. Binary Ninja inspection explicitly targets the supplied executable PE view, without byte or metadata mutation.

Executable-derived evidence:

| Original range, exclusive end | SHA-256 | Operation |
| --- | --- | --- |
| `0x1403f3e9d–0x1403f3f3f` | `a6482e55ff87221e712ccb483b7a7568f4224411afabccf74aee8298aa5976d5` | Initial grouping in actual shaping orchestrator `0x1403f3d80` |
| `0x1403a5db0–0x1403a5f46` | `eb2c8d92ccff2197f4101aa90d94f5efd9a1534c6c148d99335d53852b68801a` | Native cluster merge and equal-cluster expansion |
| `0x1403a6b50–0x1403a6bd9` | `fd8cbff3bdb4517fbfb1f5a519afead146b2db376e46a0b5da7a26f35a670416` | Native unsafe-break mask/scratch assignment |

The orchestrator keeps an explicit zero in `r12` from `0x1403f3d91`; the phase obtains the actual buffer from its context at `+0x18`. Buffer fields are cluster level `+0x44`, scratch flags `+0x4c`, input cursor `+0x84`, count `+0x88`, output count `+0x8c`, input records `+0x98`, and output records `+0xa0`. The portable API borrows typed records and does not invent an owner for these pointers.

Scratch bit 0 gates the entire phase. Starting at record zero, a following record continues the current group when its initialized category bits `CharacterState & 0x1f` are 10, 11 or 12, or when category 1 has either joiner property bit `0x100/0x200`. Otherwise it begins a new group. A single record needs no operation. Codepoint values, record order, glyph properties, private state and the full character-state word remain unchanged.

At cluster level 0, the original merge helper finds the unsigned minimum cluster within the group. It extends the group's end through adjacent equal clusters, then extends its start backward through adjacent equal clusters down to the input cursor. It writes the minimum cluster to that extended interval, clearing mask bit 0 only where the cluster changes. It does not set the unsafe scratch bit. This expansion can include records outside the property-defined group; the first-party implementation preserves that behavior.

At levels 1 and 2, cluster values remain intact. The native unsafe helper finds the group's unsigned minimum and sets mask bit 0 only on records with another cluster value. Any such record sets scratch bit `0x10`. Existing mask and scratch bits are preserved. Equal clusters alone do not create unsafe flags.

Category and joiner values come from [recovered native preprocessing](text_unicode_preprocess.md); this phase does not query host Unicode or guess a Unicode version. Cluster-level names and the unsafe-break flag meaning use the [primary HarfBuzz buffer API](https://harfbuzz.github.io/harfbuzz-hb-buffer.html#hb-buffer-cluster-level-t) as format references. No implementation, table or runtime dependency was copied. The source's three level values are explicit; later API additions are not silently accepted.

Validation: four strict C++20 O1/dead-strip groups pass with ASan/UBSan and warnings treated as errors. Fixtures cover unsigned minima, expanded boundaries, changed-cluster bit clearing, retained clusters, marks and joiners, equal-cluster no-ops, scratch gating, empty input and preflight rejection. The first-party limit is 4,096 records: equal-cluster expansion may revisit earlier records, and this safety bound caps total work. The pass allocates no runtime storage.

The research-only Unicorn 2.1.4 harness runs the unchanged phase slice and its actual helpers, with the observed caller registers and zero input/output cursors. It compares 2,176 runs, 81,544 complete records and 409,896 serialized words including returned scratch flags. Coverage includes all 32 category-bit values, four joiner-bit combinations, both scratch gates and all three levels; 1,024 random runs cover repeated, unsigned and nonmonotone clusters, arbitrary masks and metadata. Another 384 runs consume 4,269 records initialized by the unchanged original preprocessing/default callbacks. Output SHA-256 is `fcc24c37bf905e18fabd4a65823e44166b55270744913852a058a39d16ec917e`.

Every executed block is whitelisted. Original code/tables are read-only. The harness checks unchanged codepoint/property/private/character fields, retained mask bits, unrelated caller-buffer and Unicode-owner bytes, output fences, the phase stack and observed caller registers. Only 1,792 bytes of replay samples and compact receipts are retained; no game process, window, audio, font copy or OS/game callback is involved.

The preceding `0x1403f2fa0` stage may insert a dotted circle through font callbacks and buffer growth. Its `0x1403a69b0` finalization can leave prior output storage/count; general merge backfill into that storage is outside this span-only contract. This remains unsupported rather than replaced by a fallback. Direction/script decisions immediately after `0x1403f3f3f`, positions, normalization, substitutions, complete shaping and final-frame fidelity remain open. The next pure recovery target is the direction-driven record-group reversal at `0x1403f3f3f–0x1403f400b`.
