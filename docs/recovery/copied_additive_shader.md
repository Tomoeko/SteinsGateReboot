# Copied-destination additive reference shader

[Readable HLSL](../../shaders/reference/copied_additive.hlsl) recovers native pixel program 1205, selected for the RGBA/multiplied-color/no-auxiliary group and blend mode `0x100`. The reference executable is `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

| Native program | File offset | Preferred-image VA | Bytes | Original/compiled SHA-256 |
| --- | --- | --- | ---: | --- |
| PS1205 | `0x881c30` | `0x140883630` | 1,188 | `25b33897c9c9dd667f06eae6ad8b59e1f8cbcd902345bf443d87906167395450` |

The runtime pointer/length getters `0x140566440` / `0x140566450`, called by the 2,077-program initialization loop `0x14054cde0`, establish this identity. It is occurrence 1,839 in the file-order inventory; runtime program indices and occurrence indices differ. All 2,089 original vertex/pixel addresses, lengths and hashes match a separate bounded native-getter map, trace SHA-256 `4a063fbd86b44ba52169cfc0f6e5b931689dbc84b778b7dbfabe2f7f13bbe02f`.

The shader samples source `colorTexture` / `colorSampler` at slot 0 and copied destination `cDestTexTexture` / `cDestTexSampler` at slot 2. Its sole `float4 screenMulOffset` constant converts `SV_Position.xy` to copied-texture UVs. It caps `source.rgb * vertexColor.rgb + destination.rgb` at 1, then interpolates the difference from destination by `source.a * vertexColor.a`. The shader emits that source opacity as alpha; device blend state controls the eventual target alpha separately. Copied destination alpha is unused.

The formatted source SHA-256 is `e8035666d97acec085b2d9c4d9c0a702d61e084302284566d115ee70b96264b3`. A crash-guarded console-only CrossOver helper compiled it with the user-supplied `D3DCompiler_47.dll` SHA-256 `4b074a3976399dc735484f5d43d04b519b7bdee8ac719d9ab8ed6bd4e6be0345`. Entry `main`, target `ps_4_0`, Flags1/Flags2 zero, no macros/include handler and source name `recovered_shader.hlsl` reproduce every original container byte, including reflection, signatures, instructions and statistics. This establishes compiler compatibility, not the original compiler build. The helper entered no game/graphics/audio routine and was pruned after its bounded 2.37-second run; compact source/output/hash receipts remain ignored.

The source is readable and editable; substantial edits need a fresh certificate. Texture-slot restoration, native shader/device coupling, the remaining copied blend/filter variants and Windows-versus-Metal pixels are separate checks. No Metal translation or native pixel parity is claimed here.
