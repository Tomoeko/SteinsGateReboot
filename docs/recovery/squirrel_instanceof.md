# Squirrel optional-class comparison

Target `sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`; explicit Binary Ninja view `candidate:view:1:UEU`.

Original opcode42 at `0x1402573ee` requires the right operand's class tag `0x08004000` and the left operand's instance tag `0x0a008000` before calling ancestry helper `0x140264be0`. If either tag differs, it assigns the original false Boolean at `0x140968ea8`. It raises no type error. Valid instances traverse their actual class and its parent links at +38. Assignment retains the original alias and reference-release order.

The original New Game flow opens `prologue01_en.webm`, then `BasicPicture._updateImagePosition` line2068 compares its picture against optional `Live2DPicture`. A non-class right operand exposed the rewrite's extra exception before movie visibility/position setup could finish. The correction returns the native false result; it adds no replacement renderer or class registration.

An unchanged-instruction witness executes opcode42, the real ancestry helper and result assignment through the next dispatch boundary. All318 cases pass: ten operand kinds, related/unrelated classes, two actual instance classes, separate output and output aliases of either operand, and varying VM register bases. It executes8,448 instructions across71 byte-verified PE instruction sites, with typed synthetic VM/register/object layouts and original PE Boolean constants. Positive reference counts deliberately avoid destructor dispatch. Malformed cyclic ancestry and whole native VM execution are outside this witness.

The connected C++ fixture compares host and actual bytecode results against those retained original vectors. It fails before the correction with the extra exception and passes afterward; the three focused VM/Movie sanitizer targets pass. Private inputs, script bytecode census and the unchanged-code witness remain ignored.
