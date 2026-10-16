# Prepared CFF blue zones

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are executable virtual addresses.

`cff_blue_zones` reconstructs native CF2 blue-zone normalization/construction `0x1403c49c0`. Inputs are already decoded, integer font-unit arrays and quantized 16.16 scalar values. It does not parse a private dictionary, execute the hinted glyph loader, choose an active mask or lock a glyph's stems.

## Recovered behavior

BlueValues contributes paired intervals: the original first pair is a bottom zone whose flat edge is its top; later pairs are top zones whose flat edge is their bottom. OtherBlues contributes bottom zones. Negative-height intervals are skipped. Top-zone bounds include twice the vertical darkening; bottom-zone bounds retain original coordinates. At most 12 zones fit (14 BlueValues and 10 OtherBlues coordinates).

Family substitution changes the flat edge while retaining the zone bounds. Top zones consider the lower edges of FamilyBlues after its first pair, including twice darkening. Bottom zones consider the upper edges of FamilyOtherBlues followed by the first FamilyBlues upper edge. The closest candidate must be **strictly within** one device pixel in character space. Equal distances retain the earlier candidate.

The largest valid zone height limits BlueScale by its fixed reciprocal. Vertical scale strictly below the resulting BlueScale enables overshoot suppression. Boost is `0x999a − round(0x999a × scale / BlueScale)`, capped at 32767; stem-darkening mode clears boost without clearing suppression. Flat device edges use signed rounded multiplication, direction-specific boost and native floor-to-pixel after adding half a pixel.

LanguageGroup 1 with no BlueValues, or exactly four values entirely outside strict character bounds −120/880, uses two synthetic locked ghost edges. Lower character position is `−120×65536−1`; upper is `880×65536+1+2×darkening`. Their rounded device positions include outward half-pixel margins. This path returns before ordinary zones, family substitution and suppression.

Native private inputs are reached through decoder `+0x3f0`, with array counts at `+0xb4..+0xb7`, arrays at `+0xb8/+0xf0/+0x118/+0x150`, BlueScale/Shift/Fuzz at `+0x178/+0x17c/+0x180` and LanguageGroup at `+0x204`. Native BlueScale is stored with an extra ×1000 factor and divided by fixed 1000 at `0x1403c49fd..0x1403c4a0a`; the authored interface receives the normalized scalar. Exact serialized-real-to-native quantization remains a separate decoder acceptance check. Default normalized 2597 corresponds to the CFF default 0.039625; raw values/defaults are described by Adobe's [CFF specification, Private DICT operators](https://adobe-type-tools.github.io/font-tech-notes/pdfs/5176.CFF.pdf).

## Validation and remaining work

Unchanged native construction ran in preallocated private CPU storage without a game launch, imported allocation or substituted callbacks. One thousand independent fixtures include all 88 original CFF dictionary profiles plus bounded synthetic family zones, negative intervals, darkening and scale thresholds. The authored C++ matched all 43,180 meaningful coordinate/flag/threshold words: 92 em boxes, 399 suppression cases and 1,110 family substitutions. Native output SHA-256 is `e6b558132233e2ea14580d034074c06b7108213187b7ad8e3c16665872140bf4`.

Focused tests retain strict family distance/tie rules, signed rounding, top-zone darkening, suppression/boost separation, maximum-height BlueScale reduction and em-box epsilon/margins. Project warning flags including conversion/sign conversion, ASAN/UBSAN and formatting pass. Inputs are paired/bounded and outputs use fixed owned storage. Unsupported language groups, negative thresholds/darkening and signed fixed-point overflow are rejected. Extremely small ordinary scales with an unrepresentable character-space reciprocal fail explicitly instead of reproducing native overflow.

Prepared blue-zone values are one component of hinting. Private-dictionary numeric conversion, active masks, stem locking, actual hinted contour emission, font fallback/cache ownership, glyph coverage/stroking and atlas/shader output remain open. These numeric proofs do not establish native pixel fidelity.
