# Original Unicode category and combining data

`QueryUnicodeProperties` recovers the supplied executable's general-category and canonical-combining values. Every Unicode codepoint has passed an exact C++ comparison against execution of the unchanged original default callbacks. This is a classification prerequisite for the glyph pipeline; complete preprocessing and shaping remain open.

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are original virtual addresses; Binary Ninja inspection explicitly targets the correct PE view without byte/metadata mutation.

Executable-derived behavior: buffer reset `0x1403a65e0` calls getter `0x1403f5050`, which tails to the default Unicode-functions owner initializer `0x140417ad0`, and stores the owner at buffer `+0x38`. The initializer installs combining callback `0x140417a30` through setter `0x1403f5070` into owner `+0x48`, and category callback `0x140417a90` through setter `0x1403f53a0` into owner `+0x58`. Original preprocessing `0x1403f4670` calls those slots with the current codepoint. The callbacks tail through `0x1404189a0/4189e0` to pure record lookup `0x1404185c0`.

That lookup selects a page byte by codepoint `>>8`, then a 16-bit block by `(page<<5)+((codepoint>>3)&31)`, then a 16-bit record index by `(block<<3)+(codepoint&7)`. The record is six bytes. Category and canonical combining class are bytes zero and one. Out-of-range codepoints `>=0x110000` use record zero, whose category/combining pair is unassigned/zero. Surrogate/private-use classifications are data queries here; this does not change UTF-8 decoder validation.

| Original address | Bytes | SHA-256 |
| --- | ---: | --- |
| `0x14062e0b0` dictionary | 6,024 | `69bab9637006c097bbb88c69c071478f4b57d76b75145c6dbe4e43fd76d9e5e4` |
| `0x1406308a0` pages | 4,352 | `3848be0b99d12412ab80d3e2f143a8fc291710125c235360450f220a4b27495d` |
| `0x1406319a0` blocks | 10,688 | `a51cf043af17b77eb658a1d5448bb4bf75ce8732b8d995e3f20df96e109a277b` |
| `0x140634360` record indices | 23,104 | `24f8dfa5e970d5dfaea0864759bd6e651c09969265e34b7708349a81794869ec` |

The dictionary contains 1,004 records. The space between it and the page table includes unrelated mirroring/composition data and is not treated as dictionary rows. Array extents come from the original accesses and actual maximum indices, not nearby symbol-name guesses.

Direct byte observations: the first-party generator verifies those exact native-view hashes against original executable sections and emits 3,927 readable inclusive category/combining intervals covering all 1,114,112 codepoints. The authored C++ uses an immutable array, checks contiguous full coverage at compile time, and performs a bounded binary search without allocation or host Unicode queries. Codepoints outside the original limit return the exact record-zero pair.

Validation: four strict Apple Clang C++20 test groups pass with ASan/UBSan and conversion, sign-conversion, shadow and general warnings treated as errors. Explicit fixtures exercise all 30 general categories, combining-class differences, ignorable characters whose combining class is zero, adjacent interval boundaries, surrogate/private-use ranges, noncharacters, supplementary planes and unsigned out-of-range values.

The differential harness runs the unchanged original default callbacks `0x140417a90/417a30` and their helpers for all 1,114,112 codepoints plus six out-of-range probes, including `0xffffffff`. It compares 2,228,224 category/combining bytes exactly with the first-party sanitizer executable in bounded 4,096-codepoint chunks. Native, C++ and generated property-stream SHA-256 all equal `fdbbb4947e270547dfbd19cc67d8b323986978c05250ba8d6f859b9ba6194143`. All 30 categories occur in the native results.

The research-only Unicorn 2.1.4 harness maps unchanged original code and tables read-only, validates every executed block against the five original helpers and a first-party assembly caller, checks output fences and return-stack/cursor invariants, and calls no OS or game services. Each default callback executes 1,114,118 times; record lookup executes 2,228,236 times. Only compact hashes, classification samples and receipts are retained. This establishes these two query results, without a game process, window, audio or final-frame capture.

General-category names and numeric order are format references from the [primary HarfBuzz Unicode API](https://harfbuzz.github.io/harfbuzz-hb-unicode.html#hb-unicode-general-category-t). No library implementation, source tables or runtime dependency were copied. Runtime values derive from the supplied executable. Its active Unicode version has not been identified; the operating system's Unicode data is not substituted.

Preprocessing's default-ignorable flags, modified combining-class map, script/mirroring queries, composition/decomposition, normalization, cluster operations, complete shaping and final text/frame parity remain open. The recovered category/combining values alone do not close those checks. The next discriminating recovery step is the glyph-record property assignment at `0x1403f4670`, including its recovered callbacks, ignorable flags and the modified combining map at `0x140621930`.
