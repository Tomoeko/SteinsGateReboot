# CFF glyph pixel paths and bitmap plans

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Integer outline preparation and the distinct no-hint metric phase are recorded in [cff_geometry.md](cff_geometry.md).

## Native decomposition and curve bounds

Rendering at `0x140089110` loads a glyph with flag 8, then decomposes the prepared outline using `0x14039d930`. Original callbacks at move `0x140089a10`, line `0x1400899d0`, and cubic `0x1400894b0` divide signed point coordinates by 64 and negate Y before conversion to double. A final pair of cubic controls uses the first contour point as its implicit endpoint. Other contours emit an explicit closing line to their first point.

`PrepareCffGlyphPath` reproduces this cubic/on-curve decomposition and rejects invalid tags, control pairing, contour indices, point counts, and drawing-axis negation overflow. Its values are pixel coordinates with Y increasing downward; input scale and hinting have already been applied. Path commands are readable typed values.

The native path's actual bounds are computed by `0x1404255e0` and `0x140424bc0`, separately from the font loader's control bounds. The routine first includes endpoints, skips extrema work when both controls fit in accumulated bounds, otherwise solves the derivative quadratic with a sign-preserving stable root formula. It clamps roots to the unit interval, including the native NaN-to-zero behavior on constant/linear axes, and evaluates the cubic using explicit de Casteljau interpolation. Floating-point contraction is disabled to retain the original multiply/subtract/add order.

All 44,421 native path command and double-coordinate values agree across 1,000 caller-owned prepared-outline fixtures. All 4,000 actual-bound double words agree exactly with the unchanged native bounds code. The complete native decomposition and original callbacks ran with preallocated private point/path storage; no allocator, callback, or bounds function was replaced. Retained synthetic cubic extrema expose a one-ULP difference between negative and positive X extrema; focused tests preserve those exact words.

## Bitmap extent and translation planning

The numeric allocation phase at `0x1400891c8`–`0x14008922c` floors left/top, ceils right/bottom, and rejects empty integral extents before adding border padding. Its translation phase at `0x1400892cd`–`0x1400892f6` shifts the path by the negated padded left/top. `PlanCffGlyphBitmap` returns the resulting signed pixel origin, width/height, and double translation without allocating atlas or pixel storage. It validates finite ordered bounds, nonnegative border width, signed conversions, padded extents, and translation overflow.

All 7,000 validity, extent, origin, and translation values match 1,000 unchanged native allocation/translation phases. A subsequent complete streamed comparison checks every one of the 217,831 original CFF glyphs at integral size 24 with actual horizontal/vertical table metrics: 52,914,340 scaling/metric/origin values, 80,111,620 path values, 871,324 bound words, and 1,524,817 bitmap-plan values all agree. The native and portable streamed output SHA-256 is `cfd3caab4f79949135dea11aadfe5bd0d13e7ecdc204580c21786b747a1c6534`. This invokes unchanged numeric/decomposition/callback/bounds phases on normalized design points, without claiming that native hinted glyph loading has been reproduced. Empty paths retain no plan even with border padding; a geometrically zero-width subpixel bound can have a nonzero floor/ceil extent. Strict-warning and AddressSanitizer/UndefinedBehaviorSanitizer checks cover those cases and malformed storage.

## Remaining fidelity checks

Flag 8 permits hinted glyph loading; the earlier no-hint measurement proof does not establish hinted rendering geometry. The recovered path and bitmap-plan APIs accept already prepared points. Native CFF hinting, general font matrices/fractional operands, font selection and fallback, stroke/fill render settings, antialiased pixel coverage, glyph-atlas ownership/cache behavior, and resulting screen pixels remain open. No fresh initialized-game observation was used for this tranche.
