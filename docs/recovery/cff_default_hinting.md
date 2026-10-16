# Default CFF hinted glyph configuration

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are executable virtual addresses.

`PrepareDefaultCffHintedConfiguration` connects quantized original Private DICT values to the recovered default hinted/scaled contour configuration. Input glyph coordinates must already be normalized by their font matrix, with matching units per em. Returned width values, normalized blue zones, transforms, and contour state own their data.

CFF driver constructor `0x1403c3690` sets no-stem-darkening byte driver+0x3c to 1. Its eight default knot words at +0x40..+0x5c are 500/400, 1000/275, 1667/275, and 2333/0. VectorFont constructor `0x1400878a0` creates a private library through `0x140399fe0`, whose module-loading path invokes those constructors; the wrapper constructor does not set a CFF property. These are initialization facts, not a claim that later property overrides cannot exist.

Glyph driver `0x1403cc795..0x1403cc7b3` derives slot+0xf8 from the inverse of load flag bit 1 (hinting), and slot+0xf9 from the inverse of bit 0 (scaling). Original rendering requests flag 8. The setup at `0x1403c52c0` rounds the font's 26.6-output scales after adding 32 and dividing by 64 into CF2 character pixel scales, retaining an identity subsequent output matrix. For normalized 1,000-unit glyphs at size 24, both character scales are 1,573. The scaled-em guard at `0x1403c54a0..0x1403c54c0` limits the equivalent em extent to 2,000 pixels.

Whole context setup `0x1403c5c40` normalizes Private values/blue zones and computes zero horizontal/vertical contour expansion under the constructor-default property with no stroke override. Hinting remains enabled, darkening/reverse winding disabled. The authored configuration explicitly represents this default mode; alternate properties, unhinted/raw-design loading, unequal axes and bold overrides require their separately recovered setup.

The unchanged constructor, pre-parser setup, and whole context normalization ran on 160 prepared requests: em sizes 16 through 16,384 and positive integral pixel sizes 1 through 2,000. All 1,920 character-matrix, hint/darkening-mode and expansion words agree. Native vector SHA-256: `b229667d4f233cc6dd4c29751d5905b8802f2b3696f3b9f8ee5832fe984119be`. Focused tests preserve minimum/maximum scales, copied blue zones, width metadata, actual operator execution, and rejected requests. Strict conversion/sign-conversion warnings, ASAN/UBSAN and formatting pass.

This is default numeric configuration. It does not establish active application property overrides, arbitrary font-matrix normalization, a full FT_LoadGlyph implementation, or antialiased glyph pixels. No new initialized-game capture was used.
