# Motion shader program and constants

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Original virtual addresses were inspected in the explicit PE view; bytes and analysis metadata remained unchanged.

`0x14054d400` handles the ordinary family reached through `0x140532140` for pipeline phases 0, 1, 6. The existing ordinary sprite classifier remains the narrower XY/color/one-UV material interface. `motion_shader_program` recovers CPU program selection and constant preparation for all twelve layouts; it does not install additional Metal programs or certify auxiliary-target rendering.

## Layout and program selection

The twelve 32-byte rows at `0x140654880` contain three selector words, an input-element pointer and count. Their descriptor arrays at `0x14096c4f0` through `0x14096c830` directly prove six XY layouts followed by six XYZ equivalents: position only, position/color, position/one UV, position/two UVs, position/color/one UV, position/color/two UVs. Position is float2 or float3; color is BGRA8_UNORM; each UV is float2. Every element uses slot 0, appended offset and per-vertex classification. Numeric formats and descriptor fields follow Microsoft's [DXGI format reference](https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format) and [input-element reference](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_input_element_desc).

Vertex indices are 3, 0, 4, 5, 1, 2, 9, 6, 10, 11, 7, 8. Shader owners come from `0x140a974b0`. Pixel selection classifies absent textures as 0; present A8_UNORM (65), R8_UNORM (61), R8G8_UNORM (49) as 2, 3, 4; other present formats as 1. This reads original texture metadata at owner+0x44 rather than the format of a platform conversion.

Color group is 0 when attribute bit 4 is set, otherwise 8 when blend bit 0x10 is set, otherwise 4. Without an auxiliary owner, its group is 0. With one, nonzero ordinary phases use 1; phase 0 uses 3 for two-UV layouts and 2 otherwise. The operation specialization is 2 for `(blend&0xff0f)` 3/4, 1 for 5, otherwise 0. Filter offsets are 0, 3, 6, 9, 12 for kinds 0..4; unknown native filter kinds explicitly select 0. Pixel index is `(texture*12+color+auxiliary)*15+filterOffset+specialization`. Pipeline byte+0x459 forces pixel index 0 independently of these fields. Pixel owners come from `0x140a97510`.

Alternate blend-family flags 0x100 and phases 2..5 remain explicit unsupported paths. The new selector preserves the original unknown-filter branch; the existing material classifier retains its stricter supported-filter contract.

## Matrix, uniforms and caches

`0x14054e800` binds a changed vertex owner through its virtual+0x18 and updates the matrix on every call. `0x1405320d0` gives row byte+0x44 precedence: true selects identity through `0x140530b60`; otherwise byte+0x50 selects the explicit 48-byte matrix through `0x140530b40`; otherwise `0x1405308f0` composes graphics+0x80 with the final 48-byte matrix in the graphics+0x70 stack. `0x140530890` copies the three four-float rows and appends `[0,0,0,1]`. Scalar multiply/add stages preserve original float rounding; fused contraction is disabled.

`0x1405663b0` tests each reflected metadata record's pointer at+8. Available vertex-color and filter-color uniforms receive packed AARRGGBB converted to semantic RGBA, with each byte divided by 255. The target uniform calls `0x1404c5cb0`, which converts unsigned dimensions, forms centered half extents, then subtracts them and computes reciprocal widths/heights. Authored zero-dimension rejection is a safety guard; native zero-dimension exceptional output is not part of the acceptance corpus.

Vertex and pixel cache pointers at pipeline+0x460/+0x468 suppress redundant owner binds. Matrix/available uniform updates still occur on repeated calls, followed by vertex then pixel commits. Begin/End invalidate both caches. Program-index equality alone does not recover shader-owner lifetime; the authored selector returns indices and leaves actual backend owner/cache management separate.

## Validation and limits

A hash-gated unchanged-code CPU oracle covers 2,400 cases and 7,200 calls across all twelve layouts, six absent/present format cases, color/operation/filter combinations, auxiliary and copy phases, all three matrix branches plus identity precedence, reflected uniform availability and repeated/invalidated binds. It executes 2,397,186 original instructions. Strict ARM64 ASan/UBSan C++ comparisons match 42,000 ordered events exactly, including every matrix/color/reciprocal float word. Semantic trace SHA-256 `de0b2bbf16a2009b25e9512816c9ff091ef41540f64a86112f02afe08e855d6a`.

The oracle executes original selection, matrix resolution/composition/conversion, availability checks, centered-bound and scalar arithmetic. Constant-buffer copying and shader GPU commit calls are intercepted as observer boundaries; no D3D, Metal, OS, input, speaker or game process is invoked. Matrices are bounded finite inputs. NaN payload propagation, floating environment variations, shader-owner replacement, device binding, buffer dirtiness/copying, auxiliary texture lifetime, translated shader output and pixel parity remain open. Original game resources are not included in fixtures.
