# Original Unicode direction adjustment

`AdjustUnicodeGlyphDirection` recovers the record-direction phase immediately after initial clustering. It accepts the exact packed ISO 15924 script tag and native shaping direction, adjusts complete `SubstitutionGlyph` records in place, and returns the temporary direction used by subsequent shaping. The input cursor and output count must be zero, and positions must not yet be initialized. Dotted-circle/general buffer ownership remains open.

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are original virtual addresses. Binary Ninja inspection explicitly selected the supplied executable PE view without byte or metadata mutation.

Executable-derived phase and helpers:

| Original range, exclusive end | SHA-256 | Behavior |
| --- | --- | --- |
| `0x1403f3f3f–0x1403f400b` | `6d11c850e728faa284a944b86986fdd3f09b8873b4af611331fe5ec7a4e725cf` | Actual direction phase in shaping orchestrator `0x1403f3d80` |
| `0x1403a3aa0–0x1403a3bc9` | `9fc7acb88f03c8e4fa36ed5e06301431764c7d62a0cb58103e4befc287ba25e5` | Original script horizontal direction |
| `0x1403a5db0–0x1403a5f46` | `eb2c8d92ccff2197f4101aa90d94f5efd9a1534c6c148d99335d53852b68801a` | Cluster merge with equal-cluster boundary expansion |
| `0x1403a66a0–0x1403a678d` | `104dcfa9ce2d7016865cd070f9975d6174148ed9d5d7e7e5bc80bd54d4845384` | Complete 20-byte record-range reversal |
| `0x1403a6680–0x1403a6694` | `2c8413126f29b1288538e30fa41c79866b2eef7d34e9066b4e0f0d06d3ee625b` | Full-buffer reversal |

Buffer `+0x60` holds direction and `+0x64` holds the exact script tag. Native values are 4 left-to-right, 5 right-to-left, 6 top-to-bottom, and 7 bottom-to-top. The phase's direction is temporary: the surrounding orchestrator retains the original at its `+0x30` and restores it at `0x1403f4227`. The portable return value therefore belongs to shaping state, not the user's persistent font configuration.

For horizontal directions, the original helper returns 5 for exactly these 27 script tags: `Adlm`, `Arab`, `Armi`, `Avst`, `Cprt`, `Hebr`, `Hung`, `Khar`, `Lydi`, `Mand`, `Mani`, `Mend`, `Merc`, `Mero`, `Narb`, `Nbat`, `Nkoo`, `Orkh`, `Palm`, `Phli`, `Phlp`, `Phnx`, `Prti`, `Samr`, `Sarb`, `Syrc`, `Thaa`. Every other 32-bit tag returns 4, including unknown or differently cased tags. Tags pack their four bytes most-significant first; no case conversion or host Unicode/script lookup occurs in the runtime.

If a horizontal direction already matches that original helper, the phase does nothing. Vertical direction 6 also does nothing, regardless of script; vertical 7 enters adjustment and becomes 6. These are executable-derived predicates. Direction labels and packed-tag representation use the [primary HarfBuzz common API](https://harfbuzz.github.io/harfbuzz-hb-common.html#hb-direction-t) as format references. No library implementation, script table or runtime dependency was copied or substituted, and the original Unicode version is not guessed.

When adjustment is required, the phase partitions records into a base followed by categories 10–12, using the initialized low five category bits. Format/joiner records begin a new group here, unlike the earlier [initial clustering predicate](text_initial_clusters.md). It reverses every group, then reverses the complete array. Thus groups change order while records within a base/mark group retain their final relative order. Empty and single-record arrays still change the temporary direction when the gate requires it.

Cluster level 1 invokes the original merge helper before each group reversal of length at least two. It merges to the unsigned minimum, expands through adjacent equal clusters at both ends, and clears mask bit 0 only on records whose cluster changes. Group reversal happens after that mutation, so previous reversed groups participate in later boundary comparisons exactly as in the native order. Levels 0 and 2 only permute complete records. All other mask bits, codepoints, glyph properties, private-state words and the full character-state word remain attached to their original records.

The original reversal helper can also reverse position records when buffer byte `+0x82` is set. This shaping phase precedes position initialization; earlier `0x1403a5940` clears that byte. The recovered API therefore handles complete input records with positions absent, without inventing a position owner or omitting an active native position operation.

Validation: four strict C++20 O1/dead-strip groups pass with ASan/UBSan and warnings treated as errors. Fixtures exercise all native RTL cases, exact-case/default tags, matching gates, mark-only versus joiner grouping, full-word retention, level-1 merge/reversal order, vertical behavior, empty records and preflight rejection. The first-party pass rejects unsupported direction/cluster values and bounds records to 4,096 before mutation; this bounds equal-cluster expansion work. It allocates no runtime storage.

The research-only Unicorn 2.1.4 harness compares the unchanged phase and actual helpers for 5,824 runs, 83,027 full records and 420,959 serialized words including temporary direction. The tag matrix contains 112 exact/neighbor/lowercase/unknown tags across all four directions, three levels and empty/single/mixed records. Another 1,024 random runs vary unsigned/nonmonotone clusters, masks and metadata. A 768-run pipeline corpus consumes records prepared by actual original Unicode preprocessing and initial clustering. Output SHA-256 is `2908ac941d1a59d1609401749acd95157e499dca42be39a187cff405c2da9cf8`.

Original code/data are read-only and every executed block is whitelisted. The harness checks complete permutations, retained fields and masks, unrelated caller-buffer/owner bytes, fences, stack and observed caller registers. It discards emulator translations when switching adjacent phase stop boundaries so execution stops at the exact selected original boundary. Only 904 bytes of replay samples and compact receipts are retained. No game process, window, audio, font copy or OS/game callback is involved.

Glyph mapping remains open: the following `0x1403f4440` path includes Unicode mirroring, font callbacks, normalization at `0x14040bf50`, mask assignment and nominal cache publication before GDEF. Copying a prepared auxiliary word into the glyph-index field alone would not recover that mapping boundary. Dotted-circle insertion/general output backfill, initialized positions, normalization, substitutions, positioning, complete shaping and final-frame fidelity remain open. The next discriminating target is `0x14040bf50` and its exact nominal-glyph/normalization contract.
