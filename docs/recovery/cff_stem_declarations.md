# CFF Type2 stem declarations

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are executable virtual addresses.

`cff_stem_declarations` reproduces prepared tagged operands, shared width consumption and one-axis declaration storage from native helper `0x1403c59b0`. It appends glyph-owned `CffStemHint` records for the separately recovered [stem-map builder](cff_stem_map.md). No allocation occurs inside the helper; borrowed operands remain unchanged.

The Type2 dispatch table at `0x1403c9b24` routes operators 1/18 (hstem/hstemhm) through `0x1403c7fd5` and 3/23 (vstem/vstemhm) through `0x1403c8024`. Both reach the declaration helper at `0x1403c8006`, using different axis buffers and the same width-read byte/width value. An odd operand count excludes the first operand from stem pairs. If the shared width has not been read, that operand plus nominal width becomes the width; otherwise the stored width remains unchanged. Empty/even declarations preserve the caller's prepared default width and still mark it read.

Each call accumulates start delta then end delta from the supplied origin, appending without reordering negative/ghost widths. New records clear both device cache words and their cache-valid byte. The origin starts anew for each declaration call; prior declarations are retained in the same axis buffer. The authored boundary checks operand/storage counts and signed coordinate addition before changing the glyph owner.

Number tag 0 retains 16.16 bits, tag 1 converts 2.30 with signed half-away rounding, and tag 2 shifts a signed integer into 16.16. Explicit unsigned bit operations retain native 32-bit wrapping in that shift and the fraction conversion's intermediate addition/subtraction, including extreme prepared values. Signed arithmetic shifts are expressed as explicit floor division for portability.

Advance-only load bit `0x100` is transferred into decoder+0x331 at `0x1403cc8bb..0x1403cc8c9`. In that mode the helper still consumes/marks width but retains the operand stack and does not collect stems; interpreter dispatch then returns. This is distinct from outline load flag8. The returned remaining-operand count preserves that early branch. The full interpreter also ignores later declarations after a valid primary mask; that event routing is separate from this prepared helper.

One thousand unchanged native helper fixtures supplied all three number representations, both width states, nonzero origins, existing declaration records and advance-only mode. Authored C++ matched 38,135 meaningful values including 4,607 new declarations; native output SHA-256 `dd8397dcc44a84c9c71efbbb49d45e912e1e7d8e6ab6220fad5c939754a0dc78`. Another 66 full-signed number boundaries matched unchanged native first-operand conversion, SHA-256 `454ba3832bbbb7538c5d0797b45951272f023a46335c12820f6f8c801b2782d5`. Native calls used preallocated private CPU storage without allocation callbacks, imports, GPU work or a game launch.

Focused tests preserve width/axis sharing, per-call origin, empty/default/advance-only branches, tagged rounding/wrapping and failure-before-mutation. Project conversion/sign-conversion warnings, ASAN/UBSAN and formatting pass. Actual charstring operand execution, primary/counter mask timing, hinted path continuity and native raster pixels remain open.
