# Default Unicode canonical decomposition

`DecomposeUnicodeCodepoint` recovers the original default callback's single canonical decomposition step. It returns an optional pair of unsigned codepoints; a successful singleton has `Second == 0`. Absence means the original returned false without writing either caller output. The query contains 2,060 readable immutable table rows and direct Hangul arithmetic. It does not recursively expand its results or implement normalization.

The fidelity target is `STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses below are executable virtual addresses; file offsets refer to that executable. Binary Ninja analysis used the explicit original PE view, without metadata or byte changes.

## Executable evidence

The default Unicode owner initializer at `0x140417ad0` passes callback `0x140417a50` to setter `0x1403f5260` at `0x140417b7e`. The setter writes the callback at owner offset `0x78`. The callback forwards its input and two output pointers to `0x140418850`.

The helper accepts Hangul U+AC00 through U+D7A3 before querying any table. With `index = codepoint - 0xAC00` and `trailing = index % 28`, a nonzero trailing index returns `(codepoint - trailing, 0x11A7 + trailing)`. Otherwise it returns `(0x1100 + index / 588, 0x1161 + (index % 588) / 28)`. These constants and divisions are directly derived from the executable. For terminology, the [Unicode core specification's Hangul section](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/) describes leading, vowel, and trailing jamo; that reference supplies neither the recovered table nor a version identification for this game.

Other inputs use trie `0x140418550`. Inputs below `0x110000` select a page by `codepoint >> 10`, a block by `(page << 6) + ((codepoint >> 4) & 63)`, and a record word offset by `(block << 4) + (codepoint & 15)`. Larger unsigned inputs select record zero, whose header is zero. The record header's low byte must be zero and its high byte must be nonzero. All successful canonical records have one or two outputs. Payloads are UTF-16 words; the actual helper converts supplementary pairs into unsigned codepoints. The authored generation checks every selected offset and payload bound, validates surrogate pairs, and retains only successful decoded rows.

| Original table | Virtual address | File offset | Bytes | SHA-256 |
| --- | --- | --- | ---: | --- |
| Variable records | `0x140639da0` | `0x6383a0` | 28,832 | `adbc9e1c6748222aff60fe694f2f4ffe1ad1b46d0c79e8bb0b35b07976d6e22e` |
| Page bytes | `0x140640e40` | `0x63f440` | 1,088 | `25f410be4476b73ba9fffa8d1c85758e8a10a5eab6d825fe980f57d8642aa99d` |
| Block words | `0x140641280` | `0x63f880` | 3,072 | `33e93de13853c9ae54be745b6d6c7479349309c816663f1a6284bfa46110ec0c` |
| Record-index words | `0x140641e80` | `0x640480` | 15,296 | `8510e466bb30f372836b676c1faf85101572c0817cb552b82bb1a9000b4950ed` |

The non-Hangul rows contain 1,035 singleton and 1,025 two-codepoint mappings; 137 rows have supplementary outputs. U+01FA returns U+00C5 plus U+0301, U+212B returns only U+00C5, and U+AC01 returns U+AC00 plus U+11A8. These examples establish the one-step boundary. Compatibility-only inputs such as U+00A0, U+FB01, and U+FF21 return no canonical decomposition. Outside inputs do not alias a lower 16-bit codepoint.

## Validation and remaining work

Four strict C++20 ASan/UBSan fixture groups cover canonical pairs, singleton and supplementary outputs, Hangul boundaries, the one-step boundary, compatibility rejection, and unsigned limits. A bounded first-party assembly caller executed the unchanged callback, helper, and trie in Unicorn 2.1.4 with original code and tables mapped read-only. Every executed block was checked against those function ranges and the caller. Return-stack, caller cursors, and output fences were checked per chunk.

All 1,114,112 inputs from zero through U+10FFFF matched the native boolean and both output words in streamed 4,096-input chunks. Seven unsigned outside probes also matched. There were 13,232 successes: 11,172 Hangul inputs and 2,060 table inputs. Every failed input preserved both independently initialized output sentinels. The complete 13,369,344-byte Unicode-range stream has SHA-256 `c79583815d7874d64d9100dd0fb993917f92bf31f3718d4f065e172612d2e3a5` in the native run, C++ run, and independently decoded trie/Hangul model. Only that hash and 36 compact native samples were retained. Those samples were replayed; five malformed driver ranges were rejected and two empty ranges succeeded.

The original Unicode version is unidentified. Custom Unicode owners, composition at `0x1404186c0`, compatibility decomposition at `0x140418630`, recursive normalization, nominal font callbacks, complete shaping, and final frame fidelity remain open. No host Unicode fallback or normalization result is implied by this primitive.
