# Native CFF Private DICT quantization

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are executable virtual addresses. Original font identities are in [vector fonts](vector_fonts.md).

`cff_driver_hints` decodes original encoded Private DICT bytes into the integer/fixed fields consumed by the embedded CFF driver. `CffFontDictionary::PrivateDictionaryBytes` borrows the immutable sfnt storage already owning its glyph/subroutine spans. Returned driver parameters own their arrays. This preserves readable floating [CFF metadata](cff.md) without treating binary driver quantization as ordinary host floating-point conversion.

## Recovered loading and numbers

The face initializer `0x1403c2cd0` calls CFF loader `0x1403cac90`; its Private loader `0x1403ccec0` calls dictionary parser `0x1403cc010`. The 32-byte handler records at `0x14060d650` select private mask `0x2000` and destination base subfont+0xb4. Records specify Blue/Other/Family/FamilyOther arrays (operators 6/7/8/9), their count bytes and capacities 14/10/14/10; stem-snap capacities are 13 per axis. BlueScale/Shift/Fuzz destinations are subfont+0x178/+0x17c/+0x180. The authored boundary retains paired blue arrays and rejects excessive counts instead of copying the native loader's odd-count truncation.

Integer helper `0x1403cbc00` sign-extends big-endian short operands and assembles signed 32-bit operands explicitly. Decimal helper `0x1403cbcb0` reads CFF nibbles into bounded integer significand/exponent state, with no decimal-library or binary-floating conversion. Significand growth, fractional precision, exponent adjustment, pre-division truncation and saturation reproduce its actual branches. Consequently explicit `0.06` at scale zero becomes zero in this binary, while absent ExpansionFactor retains loader default `0x0f5c`; spelling/precision can matter. Fixed division `0x14039aa80` rounds ordinary signed halves away from zero.

Kind-1 real fields use scale zero followed by arithmetic fixed >>16, so negative fractional values floor. Delta arrays apply that conversion before signed accumulation. Kind-2 fields retain 16.16. BlueScale's kind-3 path uses decimal scale 3 and stores 1000 times its normalized value in 16.16; an integer operand first multiplies in wrapping 32-bit storage. Integer-to-fixed values outside ±32767 saturate to ±0x7fffffff. ForceBold's kind-5 handler at `0x1403cc252` keeps the low byte, without boolean normalization.

Defaults match the Private loader: BlueScale `0x27a000`, BlueShift 7, BlueFuzz 1, ExpansionFactor `0x0f5c`, empty arrays and zero widths/language/seed/ForceBold. `NormalizeCffDriverBlueScale` divides the stored scale by 1000 using signed nearest rounding. The dictionary decoder bounds bytes, operations, operand stack and real nibbles; validates number grammar even for unrelated operators; rejects truncated, duplicate, unused, overflowed and unsupported boundary inputs. It does not silently turn malformed operands into zeros.

## Verification and remaining work

Unchanged native numeric helpers and the whole Private parser ran in preallocated private CPU storage without imports, substituted callbacks, GPU work or a game launch. Two thousand bounded integer/decimal fixtures matched all 12,000 fixed, integer-field and byte-field words; native output SHA-256 `cf37d4071f4015f64df4555242669f1dd2c82a08f8b37b9c458a6b60d527f4f2`. All six original CFF resources were streamed, preserving their exact raw Private ranges: 88 dictionaries matched all 2,243 driver parameter words, native SHA-256 `77773d70deb905f0bc797435ced4116bf558dc48cffff14af9772f44f1c0a14e`. Duplicate font data was removed after inspection.

Focused fixtures preserve native precision, exponent/short encodings, integer saturation, BlueScale wrapping, negative flooring, low-byte ForceBold, defaults, delta arrays and native stem capacity. Project conversion/sign-conversion warnings, ASAN/UBSAN and formatting pass. This establishes numeric preparation only. Type2 declaration/mask execution, hinted contour emission and final native raster pixels remain open.
