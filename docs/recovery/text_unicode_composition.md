# Default Unicode pair composition

`ComposeUnicodeCodepoints` recovers the original default Unicode owner's ordered pair query. Its result distinguishes `Composed` (true, write the returned codepoint), `MissingPair` (false, write zero), and `UnmappedInput` (false, preserve the caller's existing output). The immutable first-party data contains the original mapped input ranges and 940 successful ordered table pairs. No pair is inferred by reversing decomposition.

The target is `STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are executable virtual addresses; file offsets refer to that executable. Analysis used the explicit original PE Binary Ninja view without metadata or byte mutations.

## Executable evidence

The default Unicode initializer `0x140417ad0` passes callback `0x140417a40` to setter `0x1403f5110` at `0x140417b69`. The setter installs it at Unicode-owner offset `0x70`. The callback moves the output pointer into `rcx` and enters `0x1404186c0`; both input keys remain full unsigned 32-bit values.

The helper checks two Hangul gates before any table search. Any first input U+AC00 through U+D7A3 and second input U+11A7 through U+11C2 succeeds with `first + second - 0x11A7`. The original accepts a syllable that already has a trailing jamo and accepts the zero trailing index U+11A7. Thus `(AC00, 11A9)`, `(AC01, 11A8)`, and `(AC02, 11A7)` all return U+AC02. `(D7A3, 11C2)` returns U+D7BE. These observed gates are preserved exactly.

Otherwise, first input U+1100 through U+1112 and second input U+1161 through U+1175 succeeds with `0xAC00 + ((first - 0x1100) * 21 + second - 0x1161) * 28`. Boundary failures continue into the original pair tables; the authored query preserves that order.

Other pairs call original search `0x14058fa50` twice with range comparator `0x140418530`. Each eight-byte row holds an unsigned start, a signed 16-bit inclusive end delta, and a signed 16-bit base index. All observed nonpadding rows have nonnegative deltas and indices. The original searches 211 first-key rows and 40 second-key rows, each ending in an all-zero row after the ordered data. The exact original search model was checked for every Unicode-range key. Those tail rows map no input; the readable arrays retain the 210 and 39 ordered ranges. They map 376 and 62 keys to contiguous indices.

When both keys map, `cell = first_index * 62 + second_index`. The first lookup selects `level1[cell >> 3]`; the second selects `level2[level1 * 4 + ((cell >> 1) & 3)]`; the output is `scalars[level2 * 2 + (cell & 1)]`. Every one of the 23,312 reachable cells was decoded with bounds checks. There are 940 nonzero cells, including 12 supplementary outputs, and 22,372 zero cells. The original always writes this cell value and returns whether it is nonzero. If either key is unmapped, it returns false without writing output.

| Original table | Virtual address | File offset | Bytes | SHA-256 |
| --- | --- | --- | ---: | --- |
| First-key ranges | `0x1406300c0` | `0x62e6c0` | 1,688 | `5f271cd772fd8a299236f72fe0083a83fa03e0ff5933b2a056ffe3a377d33566` |
| Second-key ranges | `0x140630760` | `0x62ed60` | 320 | `3441fcfce92b6647a1f942b37517841bc0b145a9227fa1deb49d461183b53874` |
| First cell lookup | `0x140645a40` | `0x644040` | 5,840 | `6ca3d620487f209a4b57b95dc538215c44ef8c6a6a4b56e9865582c947cecdcb` |
| Second cell lookup | `0x140647110` | `0x645710` | 4,528 | `a0f82c9d017d2492c5225f380590fadd51f9f5793c1aaecddd6dcc187a70058c` |
| Reachable output scalars | `0x1406482c0` | `0x6468c0` | 5,856 | `495642c6372f67f29fa122ec5366056dae14243ea7a7547e34ac4b3a8ee03177` |

## Validation and open scope

Four strict C++20 ASan/UBSan groups cover actual Latin, kana, and supplementary pairs; Hangul gates; both failure outcomes; rejected or reversed pairs; and full-width unsigned keys. Compile-time checks validate range ordering, pair ordering, mapped pair inputs, and scalar outputs.

Unchanged native callback, helper, search, comparator, guard trampoline `0x14059b308`, and PE-initialized return leaf `0x140030f20` ran with original code, data, and guard pointer `0x1405bd938` mapped read-only in Unicorn 2.1.4. The first-party bounded caller checks input immutability, input/output fences, return stack, and cursors. Every executed block belongs to those exact functions or the caller. No guard or callback was stubbed or patched.

The streamed proof compares 2,578,498 pairs: every table cell; all 312,816 syllable/trailing and 399 leading/vowel combinations; both complete 1,114,112-input Unicode axes with a known mapped opposite key; 1,191 boundaries; all 940 reversed successful table pairs; 7,520 full-width probes; and 4,096 deterministic mixed pairs. Native booleans and written or untouched output words match C++ and an independent original-table model exactly. The 20,627,984-byte output stream has SHA-256 `540f1124ddaca09cc17dd784a6442f79dc866b728735bdf8b04f01a3dbc5693c`. All 940 reversed table pairs fail. High bits are preserved in input comparisons; a full-width input succeeds only when its actual key is present or passes a Hangul gate.

Twelve additional direct native probes confirm rejected pairs such as `(0308, 0301)`, `(0915, 093C)`, and `(1D157, 1D165)`, pair order, the three U+AC02 collisions, and the U+D7BE Hangul result. Their output SHA-256 is `b367472ba1f2efed05a70ba92a3c321861f91e3564402691afb8e2aea9825ed2`. The 34 streamed samples and 12 direct samples were replayed. Eight malformed proof-driver requests were rejected; an empty request succeeded. Full streams and raw table dumps were not retained.

This is the exact current default pair primitive. The game's Unicode data version remains unidentified. Custom Unicode owners, compatibility decomposition, recursive normalization and blocking rules, nominal font callbacks, full shaping, and final frame fidelity remain open.
