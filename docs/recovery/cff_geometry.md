# CFF geometry and native glyph metrics

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Font metadata and raw Type 2 evidence are recorded in [cff.md](cff.md); initialized native font-owner observations are recorded in [vector_fonts.md](vector_fonts.md).

## Integer design outlines

The CFF driver's no-hint callbacks are line `0x1403c4fe0`, cubic `0x1403c4ea0`, and closure `0x1403c5040`. Their builder helpers at `0x1403c9d80`, `0x1403c9ed0`, and `0x1403c9e10` preserve contour order, emit on-curve tag 1 and cubic-control tag 2, remove one repeated closing on-curve endpoint, and discard one-point contours. A move alone allocates no point. A closing cubic can retain its two final controls and use the first contour point as its implicit endpoint.

The callback storage converts signed fixed coordinates with an arithmetic shift by 10. The authored `PrepareCffDesignOutline` operates on already integral design coordinates. Its reader rejects fractional coordinates and values outside the native signed fixed-coordinate range instead of choosing an unverified conversion from general Type 2 fractional operands. Point counts are bounded by the native signed 16-bit outline range; command order and closure are validated.

All 217,831 original CFF glyphs pass point normalization, producing 25,368,015 points across 1,263,248 contours. Maximum per-glyph counts are 905 points and 42 contours. The independently decoded original corpus has zero fractional design points, zero move-only contours, and maximum absolute design coordinate 2,927. The unchanged line/cubic/closure helpers agree on all 38,628 point, tag, contour-end, and count values from 1,000 caller-owned fixtures. The fixtures include fractional callback inputs to independently check signed floor conversion; public geometry receives their resulting integral coordinates. No original allocator or drawing callback was replaced.

## No-hint point scaling and bounds

The prepared point phase `0x1403ccdf4`–`0x1403cce9d` scales signed points and advances using `FT_MulFix` at `0x14039cda0`. Disassembly subtracts one when the product is negative before adding 32,768 and shifting by 16. Half values therefore round away from zero. This differs from the later negative shaping-advance rounding documented in `vector_fonts.md`.

Native control bounds at `0x14039e220` include every cubic control point. Width and height are maximum minus minimum, horizontal bearings are minimum X and maximum Y, and vertical bearing X is minimum X minus the scaled horizontal advance divided by two with truncation. Empty outlines have zero control bounds.

With real vertical metrics, the native loader keeps the signed `vmtx` side bearing unscaled at slot `+0x48`, while scaling vertical advance at `+0x4c`. The shaping origin callback `0x1403a7500` returns horizontal-bearing X minus vertical-bearing X, and the retained unscaled side bearing plus horizontal-bearing Y. The public metrics type names the unscaled field `VerticalSideBearingFontUnits` explicitly. `ScaleCffGlyphOutline26Dot6` accepts distinct positive X/Y scales, preserves point tags and contour endpoints, and checks coordinate, bound, and origin overflow.

All 41,616 prepared-point, metric, and origin words agree with unchanged native instructions across 1,000 bounded fixtures. Connecting the first-party CFF outline reader, normalized point geometry, actual `hmtx`/`vmtx` values, and this numeric phase reproduces all 72 vertical offset values in the retained 36 initialized native-owner captures. The checked font is Source Han Sans TC, SHA-256 `44af35f7ecf9947a02ae24eb6c254bdb9f1b91c8fa7c801d701ff8a5a6bbeb3c`. Focused synthetic safety tests and both differentials pass AddressSanitizer and UndefinedBehaviorSanitizer with strict compiler warnings.

## Remaining pipeline

These APIs represent normalized integer points and the no-hint control-metric phase. They do not implement general fractional Type 2 coordinate preparation, arbitrary font-matrix transforms, hinted outlines, absent-vertical-metric synthesis, shaping outside recovered features, drawing-axis conversion, curve extrema for bitmap allocation, or rasterization. Native rendering at `0x140089110` loads glyphs with flag 8, decomposes the outline through `0x14039d930`, flips Y, divides point coordinates by 64, and computes path bounds before atlas allocation. Recover that rendering path and its numerical bounds separately before making pixel-fidelity claims.
