# FT-backed nominal glyph acceptance

`QueryVectorFontNominalGlyph` recovers nominal glyph acceptance for the game's constructor-selected Unicode character map. It borrows an existing `SfntFont` reader and its immutable resource bytes. A nonzero mapped index returns an optional unsigned glyph ID. Glyph zero returns absence, matching the original callback's false result without writing its caller output. The query neither selects another map nor supplies another font.

Target: `STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. All addresses are executable virtual addresses in the explicitly selected original PE Binary Ninja view. Original bytes and analysis metadata remain unchanged.

## Callback and selected map

FT-backed font-function preparation `0x1403a6f50` installs nominal callback `0x1403a7590` through setter `0x1403a53b0`, which writes function offset `0x130`. Callback userdata contains the actual FT face pointer at offset zero, load flags at offset eight, and a symbol-encoding byte at offset `0x0c`. The nominal callback reads the face pointer and symbol byte; load flags and the HarfBuzz font argument do not affect this lookup.

The callback calls `0x14039b140` with the full unsigned codepoint. That helper uses the face's currently selected map, calls its original class lookup, and converts an index outside the face's glyph count into zero. Callback `0x1403a7590` writes a nonzero index and returns true; it returns false without modifying the output when lookup returns zero.

When the symbol byte is nonzero, a failed lookup for a codepoint at most `0xff` retries `codepoint + 0xf000`. That branch remains outside this query's supported profile. Original vector-font constructor `0x1400878a0` selects the first map with `(platform, encoding)` equal to `(0,3)` or `(3,1)`; those are Unicode maps, so the symbol byte is zero. `SfntFont` already enforces this proven selection and supports its format-4 and format-12 mappings. Default-map selection for a font lacking that preferred map is still unsupported. See [the font reader's native mapping evidence](vector_fonts.md) and [the vector-font owner](vector_font.md).

The actual format-4 class is `0x140620500`, with initializer `0x1403e77d0`, lookup wrapper `0x1403e7c40`, and sorted-map helper `0x1403e9af0`. The format-12 class is `0x14061fd10`, with initializer `0x1403e2d10`, wrapper `0x1403e2f10`, and lookup helper `0x1403e9080`. Format 4 rejects inputs at least `0x10000`; it does not search only the low word. Format 12 compares the full key to its actual groups. Alternate supplementary maps do not broaden a selected BMP map's coverage.

## Independent native comparison

The isolated C++20 proof links only this query, its fixture harness, and the unchanged public SFNT translation unit, all compiled with strict warnings, O1, ASan, and UBSan. Four fixture groups cover direct and glyph-array mappings, glyph-zero absence, supplementary/full-width inputs, both preferred platform profiles, selected-map isolation despite an alternate map, absence of symbol retry for a Unicode map, borrowed-byte immutability, and repeated lookups.

A bounded first-party Windows x64 caller executes the unchanged actual nominal callback and FT character lookup against caller-prepared selected-map/face/userdata fields. It invokes the original format-4/12 initializers and class lookup helpers. The original code/classes and supplied map bytes are mapped read-only in Unicorn 2.1.4. Executed blocks are whitelisted, and output fences, return stack, caller cursors, face/userdata immutability, and map-byte immutability are checked. Native map-cache updates remain in the prepared map context. No callback, class method, allocator, guard, or instruction is replaced.

All 2,883,680 native results match the C++ query and an independently decoded map model. Two bounded authored format-4/12 fonts cover every input from zero through U+10FFFF and eight supplementary/outside probes each. All ten original fonts are streamed through the existing archive reader without retaining font copies; each selected map covers all 65,536 BMP inputs and the same eight probes. The complete 23,069,440-byte result stream contains the native boolean and either the glyph ID or an unchanged caller sentinel. Its SHA-256 is `661eca35332e13a917b3d3d99e247a3d43555ad99d969fceeba870f41c528621`. Original font and individual map hashes, counts, and compact samples remain in the ignored receipt. Full streams and raw maps were not retained.

## Normalization integration still required

This establishes the nominal acceptance query used at font-function offset `0x130`; it does not construct or install a native font owner. A normalizer must borrow the genuine reader from a live resource-pinned font and map absence back to the native false/untouched-output policy. Unicode mirroring accepts a replacement only when this callback succeeds, while recursive decomposition tests the second component before the first and preserves the original emission order.

Variation callback `0x1403a7600`, installed at `0x138`, calls `0x14039af00`; it requires actual Unicode map/format-14 discovery and ownership through `0x1403a14b0`. The [borrowed variation lookup](text_variation_glyph.md) now passes its scoped native comparisons; its actual normalizer/owner binding remains open. The symbol/default-map profile, recursive decomposition `0x14040c630`, emission `0x14040cef0`, buffer growth/error/publication, full normalization, and final shaping/frame fidelity remain open. Existing font parsing failures retain their contextual errors and are not converted into missing-glyph results.
