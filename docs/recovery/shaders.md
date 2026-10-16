# Shader recovery

## Evidence scope

The supplied `Files/STEINS.GATE.REBOOT/sgre_steam.exe` has SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. A bounded container inventory found 2,089 embedded DXBC occurrences, 1,832 distinct SHA-256 values, and 2,620,312 container bytes. There are 2,077 `ps_4_0` occurrences and 12 `vs_4_0` occurrences; deduplication leaves 1,820 pixel and 12 vertex programs. This is an executable inventory, not proof that every program is selected at runtime or that external/generated programs cannot exist.

Each inventoried container carries `RDEF`, `ISGN`, `OSGN`, `SHDR`, and `STAT`. Its RDEF creator is `Microsoft (R) HLSL Shader Compiler 10.1`, and its raw RDEF flags are `0x100`. The first occurrence is at file offset `0x654f20`, loaded VA `0x140656920`, length 1,740, SHA-256 `5b7aa98e0982a6a81fc0a4c3e455b6b80ef8f216cf9db01ff0e44b43401acfcd`. The last begins at file offset `0x8db680`, loaded VA `0x1408dd080`. Offsets and VAs are different coordinate spaces; the private manifest maps every occurrence using the PE sections.

[Microsoft's target documentation](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/specifying-compiler-targets) identifies `ps_4_0` and `vs_4_0` as supported compiler targets. Its [compiler-flag documentation](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/d3dcompile-constants) describes legacy preshader control and floating-point strictness separately. The observed flags and creator text do not recover the complete compile request, source, macros, include closure, or exact compiler build.

## Compiler reference

The user-supplied research target `Codex_Testing/tools/D3DCompiler/D3DCompiler_47.dll` is PE32+ x86-64, version `10.0.18362.1`, SHA-256 `4b074a3976399dc735484f5d43d04b519b7bdee8ac719d9ab8ed6bd4e6be0345`. Its adjacent PDB has SHA-256 `6758bf456900a7bc43cb7ba01f60460473db005332dd3088a198e60b6c3489f0`.

Direct file observations show CodeView GUID `ce74007d-5e07-06ec-e3e2-c09649d05eea` and age 1 in the DLL's debug-directory record at file offset 4,090,716. PDB information stream 1 has the same GUID but age 2; DBI stream 3 has age 1. Record this age discrepancy rather than declaring an unquestioned exact executable/PDB pair. Binary Ninja loaded the adjacent symbols and exposed named compiler functions.

Read-only Binary Ninja analysis of this DLL confirmed the same creator text at VA `0x1803a63a0`. `D3DCompile` at `0x1800ee230` forwards to `D3DCompile2` at `0x1800ee2b0`; the latter tests `D3DCOMPILER_USE_REPLAY` before selecting named replay/save paths. These observations identify the available research tool's behavior. They do not prove this build generated the game's shaders. No additional compiler was downloaded because an exact replacement has not been identified by evidence. No binary bytes or analysis metadata were edited.

## Raw DXBC tooling and current output

`DXBCSandbox` has generic lossless DXBC, stage-contract, semantic IR, and HLSL-emission APIs below its Unity 2021.3.35f1 importer. A private C++20 adapter under ignored `Files/research/shaders/tools/` uses those APIs directly. It builds the sibling checkout read-only into this repository's ignored research directory, disables Unity/compiler/import/visual-gate targets, and adds no game-runtime dependency. The reference checkout is GPL-3.0-only and had local modifications; retained private fingerprints identify the exact research snapshot rather than assuming its commit alone is sufficient.

The adapter validates complete-container hashes and lengths, decodes stage contracts, disassembles instructions, and imports checked SM4 RDEF field layouts into the emitter's parameter model. Its supported reflection shapes are non-array float scalar/vector fields and column-major float4x4 fields, singleton constant buffers, and float Texture2D resources. The corpus stays inside those limits. Sampler records are omitted from the Unity-shaped parameter model because RDEF supplies bindings and names but no filter/address-state values; their executable `s#` identities remain intact. A separate source-declaration adapter restores the actual RDEF sampler names by binding and emits the actual `$Globals` fields in their retained order. Unity sampler-state conventions cannot certify this game.

This declaration step addresses a measured generic-emitter mismatch. At occurrence 3, file offset `0x656260`, the original and initial baseline already had equal SHDR/STAT/signatures, but initial RDEF renamed three sampler variables and added a nonexistent `cb0_pad_8` field. The adapter preserves the instruction body, removes invented padding declarations, restores original sampler identifiers, and relies on checked native compilation to verify complete field packing and reflection. Raw emitter source is retained beside each adapted source. No original or compiled container is rewritten to manufacture equality.

For all 1,832 unique containers, lossless validation, semantic decoding, stage decoding, and baseline HLSL emission succeeded. High-level candidates emitted for 1,008. The remaining 824 explicitly rejected:

| Rejection | Unique programs |
| --- | ---: |
| Unsupported opcode | 470 |
| Analysis conflict | 325 |
| Missing metadata authority | 5 |
| Unsupported feature | 11 |
| Unrepresentable interface layout | 13 |

Every baseline, assembly listing, candidate, manifest, and status report is private and ignored. Baselines are instruction-oriented diagnostic material; their success is not source-quality acceptance. High-level emission is a candidate capability, not a compiler or semantic certificate. ASan and UBSan processed the entire unique corpus without diagnostics. Bounded wrong-magic, hash-corruption, and truncated-container fixtures reject with precise errors.

Two initial manually readable representatives retain explicit provenance: the color passthrough pixel program at file offset `0x729de0`, SHA-256 `4585cc2f570773b0d06b9eb2eef8ba0f5259192a2caf96bdbd0c890e1dbaeb16`, and the position/color/UV vertex program at file offset `0x736a30`, VA `0x140738430`, SHA-256 `4a84c1e43cf99f4e635f66d25d1074ebad5127a610953214277160896859de23`. Their manually authored readable HLSL reproduces both complete original DXBC containers byte for byte under the supplied compiler. Their names improve readability without claiming original source names where none survived.

The vertex program stores `modelViewProj` column-major in b0, but computes row-vector multiplication: each original DP4 dots the homogeneous input position against one stored column. Its verified HLSL is `mul(homogeneous_position, modelViewProj)`. For a Metal `float4x4` receiving the same bytes unchanged, the corresponding column-vector algebra is `transpose(matrix) * homogeneous_position`. This algebraic relationship does not establish the game's CPU-side matrix writes, draw binding, or Metal pixel acceptance.

## Native compiler comparison

A first-party C++20 Windows console helper was built with the already installed MinGW toolchain. It loads only the explicitly supplied compiler DLL and invokes D3DCompile. A headless CrossOver CLI batch used the existing bottle without changing its configuration, launching the game, or using window/audio APIs. Error-dialog flags and replay-environment clearing bound the helper's behavior. No SDK or implementation source was downloaded.

The tested request is entry `main`, original SM4 target, Flags1 0, Flags2 0, no macros, no include handler, and source name `recovered_shader.hlsl`. The DLL's SHA-256 and helper SHA-256 are retained with the private reports. Passing the raw RDEF value `0x100` as Flags1 was rejected as invalid; successful Flags1 0 compilations also emit RDEF `0x100`. Therefore RDEF flags cannot be assumed to equal D3DCompile request flags.

| Output kind | Compiled / attempted | Complete-container exact | SHDR exact | RDEF exact | ISGN/OSGN exact |
| --- | ---: | ---: | ---: | ---: | ---: |
| Instruction-oriented baseline | 1,832 / 1,832 | 1,575 | 1,575 | 1,832 | 1,832 |
| High-level candidate | 1,008 / 1,008 | 702 | 702 | 1,008 | 1,008 |
| Manually readable representatives | 8 / 8 | 8 | 8 | 8 | 8 |
| Formatted tracked reference shaders | 8 / 8 | 8 | 8 | 8 | 8 |

Counts overlap across kinds and are not additive. There are 1,582 unique original programs with a complete-container exact baseline or candidate; 250 have no exact source yet. All 12 vertex baselines match. Before the reflection adapter, the respective baseline/candidate exact counts were 57/41, with RDEF equal in 58/45; those compact initial reports remain private. A complete-container match compares every original byte, including header/hash, reflection, signatures, instructions, and statistics; no chunk normalization or replacement was used. Compilation acceptance, matching instruction payload, complete-container identity, and game/Metal pixel fidelity remain distinct claims. Nonmatching outputs stay failed for complete-container acceptance. All native-versus-Metal pixel checks remain open.

The eight tracked manually authored sources are the two initial representatives, the four ordinary sprite references below the RawTex vertex reference in [raw_texture_pass.md](raw_texture_pass.md) and copied-additive PS1205 in [copied_additive_shader.md](copied_additive_shader.md). The formatted versions were separately compiled and matched, with source SHA-256, compiler/helper identities, request arguments, and output hashes retained in the private corpus reports. Instruction-oriented baselines and automatically emitted candidates remain ignored research material; their byte certificates do not grant public source-quality acceptance.

## Ordinary sprite source and Metal boundary

Recovered shader selection and state bindings are recorded in [motion_rendering.md](motion_rendering.md). The ordinary sprite vertex program is VS index 1, with float2 XY, packed BGRA8_UNORM color, and float2 UV in a 20-byte input. Each output position component dots `(x, y, 1)` against one original constant register’s `.xyw`. The earlier three-dimensional VS index 7 is a separate interface.

Four additional readable, formatted HLSL sources independently reproduce their complete original DXBC bytes under the same request/compiler:

| Authored reference | Original preferred-image VA | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| `sprite_transform.hlsl` | `0x140736d70` | 932 | `f24652f5fa99c189f7824454f4b0b3190764b444ba7a5cb5c9463d0e2c7e2334` |
| `sprite_texture.hlsl` | `0x140761620` | 620 | `432c0475136871f87024a041af9eff6c04103ecbf626f0ebb1a20c24107238c9` |
| `sprite_color.hlsl` | `0x140770670` | 668 | `bbe091625a725979432e33a711fd4d2ed3d14e1a1d2e58c76fa278993d6d8556` |
| `sprite_color_double.hlsl` | `0x140780090` | 708 | `588529ae6c3a56c78c4d39508be08982791b18d58e21e649ef0647f4b97bdec1` |

The byte counts above are DXBC container lengths; occurrence indices 814, 874 and 934 are separate inventory identities. RawTex VS index 4 uses `raw_texture_transform.hlsl`, a 952-byte complete-container match at file offset `0x735ec0`, SHA-256 `5ff91389ad191d7238d7ee8066eac13547fcd670a195192f08b66f4b58f42b80`. Its 16-byte XY/UV input and uniform vertex color are distinct from ordinary VS index 1.

These four were already represented in the corpus union; they do not increase the 1,582 unique byte-exact total. Their authored names and readable operations now satisfy the narrower source-quality check. Four additional readable A8/L8 sources now reproduce complete original containers: `sprite_alpha_texture.hlsl` (PS360, 680 bytes, SHA-256 `eb4bc0fb770a1fb8e134000f0a83a8c5244ae48b0fab4783ec284671b4796bce`), `sprite_alpha_color.hlsl` (PS420, 688 bytes, `2256f870a01f74354acf42c03718e47a8d896b34165c299726cc6f43ecfd3fd2`), `sprite_luminance_texture.hlsl` (PS540, 668 bytes, `865d0c970231174b4867a20f11b5d06f3c9c197c05312167d0370cf276930e74`) and `sprite_luminance_color.hlsl` (PS600, 688 bytes, `3fdaef28e2588510167ffe55ac1bcaafc56e18b72c50032af4c61da5be2d2ceb`). Original getters `0x140566440`/`0x140566450` independently establish runtime slot identities and lengths. The same compiler/request matches every original chunk and header byte. These sources were already represented in the corpus union and do not increase its unique count. Special color filters, auxiliary targets and remaining interfaces still need their own readable-source certificates.

`shaders/metal/sprite_pass.metal` translates the recovered ordinary subset: original projection registers, texture-only/multiply/double-RGB color, A8 white-plus-alpha and L8 replicated-red interpretation, inversion/premultiplication, and normal color filtering. The first-party macOS boundary prepares pipelines/textures outside frames, uploads bounded vertex/index/constant data into three reusable slots, and preserves explicit draw order, triangle-strip/list topology, twelve blend modes, and common normalized sampler settings. It rejects unsupported shader/filter paths. Single-mip textures use an effective LOD of zero; native MaxAnisotropy 2 is inactive for both recovered non-anisotropic filters.

Actual Apple M2 Max offscreen GPU fixtures pass under ASan/UBSan: all 27 texture/color/post-operation combinations, twelve blend modes and disabled writes, nearest/linear filtering, clamp/repeat/mirror, packed color order, raw-register translation/scale, topology, and stale/foreign handles. Uniform UNORM comparisons permit one quantization level; coverage, addressing, write masks, and integer-handle checks are exact. The separate color-pass fixture verifies the VS7 matrix transpose. These bounded synthetic checks establish backend behavior; original-game D3D-versus-Metal pixel/timing acceptance remains open. Neither test creates a window or audio output.

## Next acceptance checks

Recover executable shader selection, constant-buffer writes, sampler objects, texture formats, blend/rasterizer state, clipping, and render-target conventions. Retain native draw captures before assigning a shader to a scene. Extend exact source recovery from the retained per-program mismatch ledger; resolve reflection-layout/request differences separately from source-lift and instruction-generation limitations. The eight representative matches establish compatibility for those programs, not identity of the game's original compiler build.

Translate recovered operations into readable Metal source only after those bindings and render states are identified. Preserve constant bits, lane masks, matrix packing, interpolation, sample/derivative sites, instruction ordering, and branch-mask behavior. Measure D3D versus Metal pixels and floating-point edge cases with an explicit tolerance/exception ledger. Keep unsupported lifts, all 12 vertex interfaces, uncertified candidates, and unmeasured cross-API differences open in `ROADMAP.md`.
