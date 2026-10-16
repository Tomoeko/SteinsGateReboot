# Motion sprite rendering recovery

Evidence date: 2026-10-12. Executable `sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are virtual addresses in the explicitly selected PE Binary Ninja view. Native source bytes and analysis metadata remained unchanged.

## Command and shader bridge

`0x140559dd0` copies XY floats, packed color, and UV floats into 20-byte vertices, captures texture/blend/filter state, and calls shader selection `0x140532120`, then draw submission `0x140531b70`. Render modes 0, 1, and 6 select the ordinary shader path `0x14054d400`, except blend flags `0x100` select an alternate path. Auxiliary targets and backbuffer-copy phases affect selection and remain unsupported by the authored ordinary sprite classifier.

The primitive-format table at `0x140654880` selects vertex shader index `((field1 + 2*field0)*3 + field2)`. XY/per-vertex-color/one-UV sprites use primitive format 4 and vertex shader index 1. Input elements at `0x14096c5f0` are POSITION float2, COLOR BGRA8_UNORM, and TEXCOORD float2, with appended aligned offsets. Color conversion `0x140559da0` maps resource `RRGGBBAA` words to packed `AARRGGBB`; little-endian vertex bytes are B,G,R,A. Portable commands retain the resource semantic RGBA order.

Vertex shader 1 is bytecode at `0x140736d70`, file offset `0x735370`, 932 bytes, SHA-256 `f24652f5fa99c189f7824454f4b0b3190764b444ba7a5cb5c9463d0e2c7e2334`. Its constant buffer has four float4 registers. For output component i, it computes `dot(float3(x,y,1), cb0[i].xyw)`. Thus each register is a matrix row; its Z coefficient is unused. It forwards semantic color and UV without modification. Metal matrix upload must preserve these rows explicitly.

Ordinary pixel selection reads texture DXGI format, texture attributes, blend mode, auxiliary target state, and color-filter kind. BC7/RGBA textures have kind 1, A8 kind 2, L8 kind 3, and two-channel R8G8 kind 4. The authored classifier currently supports the three original decoded texture formats. Color mode is 0 when texture attribute bit 4 skips vertex color, otherwise 8 when blend bit `0x10` doubles RGB, otherwise 4.

With no auxiliary target, native pixel shader index is `(12*textureKind + colorMode)*15 + 3*colorFilter + blendSpecialization`. Blend specialization is 2 for operations 3/4, 1 for operation 5, and 0 otherwise. Color-filter kinds are explicitly registered as NONE=0, BLEND=1, ADD=2, SUBTRACT=3, NEGATE=4 at `0x14027d19d` through `0x14027d27f`.

| Texture interpretation | Texture only | Vertex color | Vertex color, doubled RGB |
| --- | ---: | ---: | ---: |
| BC7 sampled RGBA | 180 | 240 | 300 |
| A8: white RGB, sampled alpha | 360 | 420 | 480 |
| L8: sampled red replicated to RGB, alpha 1 | 540 | 600 | 660 |

BC7 base bytecode addresses are `0x140761620`, `0x140770670`, and `0x140780090`, with hashes `432c0475136871f87024a041af9eff6c04103ecbf626f0ebb1a20c24107238c9`, `bbe091625a725979432e33a711fd4d2ed3d14e1a1d2e58c76fa278993d6d8556`, and `588529ae6c3a56c78c4d39508be08982791b18d58e21e649ef0647f4b97bdec1`. Direct DXBC instructions prove sampling, color multiplication, and RGB doubling. Specialization 1 then inverts RGB while preserving alpha; specialization 2 premultiplies RGB by the processed alpha. Color-filter arithmetic precedes these specializations. Shader slot recovery does not certify every slot's GPU translation.

`SelectSpritePipeline` returns this selection with typed vertex-color, pixel-operation, blend, and sampler decisions. Native instruction execution from `0x14054d554` through `0x14054d6c9` matched all 540 synthetic texture/color/blend/filter selections with zero differing indices. No helper replacement was required for that instruction range.

## Blend state

`0x140531d20` creates twelve pairs of D3D11 blend states from six-word rows at `0x140653350`. Rows contain RGB operation/source/destination and alpha operation/source/destination. Independent blending and alpha-to-coverage are disabled. Target 0 write mask is 15 in the first variant and 0 in the second; other targets remain zero. State byte `0x84` selects the write-mask variant. Every native alpha operation is addition.

The following names use Microsoft's [blend-factor enumeration](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_blend) and [blend-operation enumeration](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_blend_op). Table values are direct executable observations.

| Mode | RGB operation | Source RGB | Destination RGB | Source alpha | Destination alpha |
| ---: | --- | --- | --- | --- | --- |
| 0 | Add | Source alpha | Inverse source alpha | One | Inverse source alpha |
| 1 | Add | Source alpha | One | Zero | One |
| 2 | Reverse subtract | Source alpha | One | Zero | One |
| 3 | Add | Destination color | Inverse source alpha | Zero | One |
| 4 | Add | Inverse destination color | One | Zero | One |
| 5 | Reverse subtract | Source alpha | One | Zero | One |
| 6 | Add | Source alpha | Inverse source alpha | One | Inverse source alpha |
| 7 | Disabled | One | Zero | Zero | One |
| 8 | Add | Source alpha | Inverse source alpha | One | Inverse source alpha |
| 9 | Add | Zero | Inverse source alpha | Zero | Inverse source alpha |
| 10 | Add | One | Zero | Zero | One |
| 11 | Add | One | Zero | Zero | One |

Ordinary phase `0x140531550`/`0x1405316c9` selects `(blendMode & 0xff0f)` when blending is enabled, otherwise row 7. Special blend flags `0x100` force row 10 and a different shader path, which the authored classifier rejects. The passed blend factor at `0x140653470` is float4(0), and sample mask is `0xffffffff`. None of these rows use a constant blend factor.

## Texture sampler and target coordinates

`0x1404f85c0` creates and binds the per-texture sampler. Its descriptor contains explicit filter/U/V state, W=WRAP, bias 0, MaxAnisotropy 2, comparison NEVER, border float4(0), and LOD bounds ±FLT_MAX. The common nearest pair selects filter 0; the common linear pair selects `0x15`, linear minification/magnification/mip filtering. Anisotropy is disabled by both filter codes, so MaxAnisotropy must not independently enable it in Metal. Descriptor semantics follow the [Microsoft sampler reference](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_sampler_desc) and [filter enumeration](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_filter).

MMotionLayer constructor `0x140529270` initializes its two filter settings to zero. Texture registration `0x14052bb40` applies them through `0x14054b1f0`; `0x14052c710` changes both settings and updates existing textures. Several UI constructors explicitly select (1,1). Other mixed/native filter modes remain outside the authored sampler interface. Native `0x1404f8e40` maps address settings 0/1/2 to CLAMP/WRAP/MIRROR. Common filter pairs and all nine address combinations were validated by executing their original instructions.

`0x1404c5cb0` returns centered current-target bounds `[-width/2,-height/2,width/2,height/2]`, obtaining width/height from graphics root offset `0x40`, target offsets `0xc/0x10`. Logical target dimensions and their connection to game settings remain unresolved. No fixed 1920×1080 target is inferred from asset sizes. Clipping, viewport conversion, depth/stencil state, alternate render phases, mesh vertices, and native GPU differential captures remain acceptance checks.
