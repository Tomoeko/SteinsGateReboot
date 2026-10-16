# TrueType font program declarations

The first-party instruction decoder checks the original byte and signed big-endian word PUSH encodings. Its 256 instruction lengths are compared with the original PE table at `0x14060BFC0`. It borrows immutable program bytes and returns checked inline-data views.

`TrueTypeFontProgram` executes only the recovered font-declaration slice: PUSH instructions and FDEF. FDEF stores sparse function identifiers in original insertion order, replaces an existing identifier in place, and scans complete instructions to its ENDF without executing the body. Nested FDEF/IDEF and malformed instruction extents report their original error category. A top-level ENDF has no call frame and fails. Other executed instructions retain an explicit recovery boundary.

Native evidence is `sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`: interpreter `0x1403B5F40`, FDEF `0x1403B6ACD`, ENDF `0x1403B6C03`, context load `0x1403B4B80`, and font-program initialization `0x1403BBF60`. The operand allocation is declared `maxStackElements + 32`; function capacity is declared `maxFunctionDefs`. Native non-pedantic missing fixed operands are zero-filled; the explicit strict policy reports underflow. The one-MiB preparation bound is an authored resource bound, not a native maximum.

The four supplied phone fonts each declare 315 functions with IDs 0..315 except 140, using only literal pushes and FDEF at font-program top level. This permits a real bounded comparison of every native registration record. It does not establish execution of their preparation or glyph programs. The optional local comparison receives unchanged native range/start/ENDF/id/active/auxiliary records and actual original fpgm/maxp bytes; no font bytes or glyph programs belong in public fixtures.

Function bodies, size-program state, CVT/storage/twilight ownership, point movement and interpolation, original interpreter policy, hinted metrics, quadratic path construction, stroke and raster still require recovery. The accepted unhinted outline owner finalizes root translation and metrics; its output cannot substitute for the original pre-hint point zones. This module is not wired to glyph rendering and does not alter CFF or admit hinted phone text.

Validation: eight focused sanitizer groups pass. The local comparison matches all 1,260 original function records across the four supplied fonts, including capacities, completed stack, dispatch counts and immutable program ownership. Glyph-program execution remains open.
