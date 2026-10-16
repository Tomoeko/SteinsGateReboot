# Indicator text construction and measurement

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Native virtual addresses identify the explicitly selected original PE view. Reference bytes and analysis metadata remain unchanged.

## Recovered element owner

Indicator print `0x14026ca90` stops its UTF-8 input at NUL, converts it to UTF-16 through `0x140533620`, builds MFont elements through `0x1405336e0`, then measures them through `0x140533470`. `PrepareIndicatorText` receives already prepared UTF-16 units and returns owned elements. Its caller performs the print boundary's NUL truncation and conversion. This fixed-size view path is separate from the richer TextView dialogue markup/layout pipeline.

MFont construction `0x140533080` defaults color to `0xffffffff` and disables tag recognition. Ordinary clusters contain one code unit, a following unit when the first lies anywhere in `0xd800..0xdfff`, and at most one following Japanese combining mark (`0x3099/0x309a`) or selected Thai mark from mask `0x3fc003fd` over `0x0e31..0x0e4e`. This preserves the original grouping rather than performing Unicode grapheme segmentation.

When enabled, tag recognition enters state 1 after `#`. That state persists while emitting subsequent non-brace units individually, including further hashes. An opening brace enters comma-separated parameter parsing; the closing brace returns to ordinary clustering. Unknown directives and unfinished directives emit no elements. Parameters retain each unit's low byte. `color` parses the next parameter as unsigned 32-bit hexadecimal with native prefix/sign/overflow behavior; exact `clear` restores the incoming font color. `scale` parses a decimal prefix as double then narrows to float32. Neither directive resets the other style field, and extra parameters are ignored.

The original state persistence is visible in `0x1405336e0`; resetting immediately after an escaped literal produced 133 differing cases and was corrected before publication. The comparison retains this behavior, including the change in combining-unit grouping while state 1 is active.

## Fixed view measurement

`IndicatorTextMeasurement` streams actual font-provider metrics into bounded stack state without allocating; the span-based `MeasureIndicatorText` uses the same operations. Exactly one LF unit is a line break. Width sums each element's scale times its glyph width, then adds `(element count - 1) * horizontal spacing` for nonempty lines. Height uses the maximum scaled glyph height for each line, sums line heights and adds vertical spacing times the number of LF elements. Width takes the maximum line width; maxima begin at zero. Spacing is not multiplied by an element's scale. Empty lines and a trailing LF still contribute their separator spacing. Separators bypass glyph metric access.

Both operations enforce an authored 65,536-unit/element budget. Terminal surrogate consumption and missing recognized parameters fail explicitly rather than reproducing unsafe original reads. Nonfinite/overflowing layout values and hexadecimal scale syntax remain explicit unsupported inputs. Decimal conversion uses the standard library's classic locale and double-to-float ordering; extreme decimal underflow, nonfinite forms, hexadecimal floating grammar and locale variants remain outside the measured acceptance range.

## Validation and open integration

Independent strict ASan/UBSan fixtures cover owned clusters, selected combining marks, default disabled tags, persistent state, unsigned color conversion/clearing, scale prefix conversion, incomplete directives, line spacing, empty/trailing lines, negative scaled metrics, separator metric bypass and malformed-input bounds.

One thousand bounded original parser calls match all 56,699 owned text/style elements exactly across literal/tag modes, mixed Japanese/Thai/surrogate clusters, incomplete tags, color overflow/sign/prefixes, decimal fractions/scientific notation, malformed exponents and trailing characters. The unchanged parser, string/container helpers and CRT numeric conversion execute 13,721,599 instructions. Only allocation/release and private CRT thread-state access are substituted; numeric converters are original instructions. Trace SHA-256: `4a006d26eb9a7c9378982059fec1bfa38d91a08e0e081d201d7f20bd8aa5daad`.

Two thousand original measurements match all 4,000 float result words exactly after 946,494 unchanged instructions. Only the font metric callback supplies prepared metrics; line traversal, scaled arithmetic, spacing and maxima remain original. Trace SHA-256: `a4e3d2dfa8a7505a9f68db1998fee49aa7c58aefb4c5fb11644efe4dc66b8a4a`. Compact synthetic inputs/reports stay ignored under `Files/research/runtime/indicator/`; no game process, window or audio output runs.

Actual Font/PSB providers, Indicator/LayerView script ownership, metric refresh after font settings, task/render submission, animations, hinted raster and native text pixels remain open. These element/bounds checks do not establish those features.
