# Fixed Type2 hinted charstring execution

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are executable virtual addresses. Original font identities and immutable storage are recorded in [vector_fonts.md](vector_fonts.md).

`ExecuteCffHintedCharstring` executes the observed Type2 byte programs directly into the recovered [contour owner](cff_contour_owner.md). It keeps native integer/fixed number tags and wrapping coordinate arithmetic instead of converting the raw double-coordinate outline after lexical hint events. The glyph and selected local/global subroutine spans borrow immutable font storage for the call; returned callback geometry, width state, maps, masks and caches own their values.

## Native program behavior

Entry `0x1403c7a20` initializes glyph-owned operand/stream/declaration/map/path state. Its active byte loop starts at `0x1403c7f61`, with operator targets selected through table `0x1403c9b24`. Short and compact numbers retain integer tag 2; encoded 255 retains fixed tag 0. Conversion helpers `0x1403c9c40`/`0x1403c9cb0` feed fixed move/line/curve operations. The authored grouped operators preserve optional initial/final displacements and alternating axes for relative, same-axis and alternating-axis curves, including curve-then-line and line-then-curve.

Declarations share width state and append to their glyph axis before a primary mask becomes valid. Native odd leading stem operands are excluded even after width was already read. A primary mask reads exact bytes; counter masks update their separate map and initial/stem caches. A first map construction can initialize the primary mask automatically, after which later declarations are ignored. Pending segments flush before dirty-map rebuilding, including when an unchanged logical point carries a new mask. Move closes the preceding logical contour. Endchar closes pending geometry while retaining the interpreter's own final relative position separately from the owner's first-point closure.

Width handling retains a subtle state difference: an explicit endchar width updates the numeric width without setting the previous-width-read flag. Move/stem phases set that flag. Subroutine calls require an integer-tagged index, apply the selected dictionary's count-dependent bias, preserve shared operands/position, and propagate an endchar reached inside a subroutine. A root return, missing explicit valid-program terminator, malformed operand group, excessive storage/work, and unrecovered escaped arithmetic/flex/composite operators produce contextual errors rather than silent defaults.

## Native differential

A bounded prepared native operand stack, stream array, declaration vectors, pending storage, three maps, and builder allowed the unchanged active parser to run through endchar, stopping before native cleanup. Original blue/declaration/mask/map/contour functions and callbacks executed without allocator/import/callback substitution, modified executable bytes, a game launch, or GPU services. Native structure padding is excluded; meaningful cache flags are bytes.

One thousand independent synthetic byte programs matched all 125,429 callback/width/position/winding/mask/map/cache values and 6,394 actual callbacks. They cover every observed drawing group, compact/short/fixed encodings, nested local/global calls, counter-before-primary state, later ignored declarations, and width boundaries. Native output SHA-256: `55bda506b22b3be156314a23ddfc42b3581d126c78bdd1d2de9114b33e789181`.

All six original CFF resources were then streamed one glyph at a time. FontTools independently supplied exact original glyph/subroutine/Private byte ranges and dictionary routing; the authored sfnt/CFF/archive readers selected their own ranges. Native Private parsing and context normalization retained actual blue/width data. Prepared character scales were 1,573 in both axes with stem darkening disabled, corresponding to the separately verified [default configuration](cff_default_hinting.md) for normalized 1,000-unit glyphs at size 24.

Every one of the 217,831 glyphs across all 88 dictionaries matched its meaningful output hash: 15,676,790 callbacks and 140,587,798 coordinate, flag, width, winding, mask, map and cache words. Compact full-corpus oracle SHA-256: `e5a2db387faf71c17f2a553edae863d89ae649aa08eb1848dbb53be8d1d866dd`. Only hashes and compact evidence were retained; decoded font resources were processed singly. Strict conversion/sign-conversion warnings, ASAN/UBSAN, format checks, malformed-input cases, recursive-call limits, callback bounds and the byte-work limit pass.

## Remaining acceptance

The returned callbacks can feed [native fixed outline construction](cff_contour_outline.md) and the recovered pixel-path/bitmap-plan stages. Default numeric setup and this complete byte-program proof do not replace a whole FT_LoadGlyph observation. Arbitrary font-matrix normalization, nondefault properties/bold/darkening contexts, driver post-transform/metric publication, TrueType outlines/hinting, complete shaping/font fallback, Blend2D stroke/fill antialiasing, atlas cache/upload ownership, and screen pixels remain open. No CoreText or raster parity claim is implied.
