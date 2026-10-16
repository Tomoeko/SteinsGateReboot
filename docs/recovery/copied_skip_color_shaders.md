# Copied blend shaders without vertex-color multiplication

[Additive PS1129](../../shaders/reference/copied_additive_skip_color.hlsl) and [overlay PS1131](../../shaders/reference/copied_overlay_skip_color.hlsl) recover two programs required by the original model's raw blend modes `0x110` and `0x112`. Both sample the original source at `t0/s0` and the copied destination at `t2/s2`. They preserve source alpha and ignore the vertex color. Overlay chooses its low/high branch independently for each destination RGB channel at `0.5`.

The reference is `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Runtime program indices come from the recovered getter map; they differ from container occurrence numbers.

| Runtime program | Container occurrence | File offset | Bytes | Original SHA-256 |
| --- | --- | --- | --- | --- |
| PS1129 | 1763 | `0x866ae0` | 1,140 | `9e2d5eee2c5659b32c12c53cf199e7352b487db9c148b007f9be5b768bd310e2` |
| PS1131 | 1765 | `0x867400` | 1,384 | `28c17ad4b3b1da5da4019644eb8f23e1aaa34a5506b57af614fde70ae523383a` |

Both authored sources compile to the complete original container, byte for byte, using the inspected compiler DLL version `10.0.18362.1`, SHA-256 `4b074a3976399dc735484f5d43d04b519b7bdee8ac719d9ab8ed6bd4e6be0345`. The request uses entry `main`, target `ps_4_0`, flags zero, no macros/include handler, and source name `recovered_shader.hlsl`. This demonstrates compiler compatibility, not the game's exact compiler build provenance.

The compiler ran through the guarded console-only helper under CrossOver without game, graphics, or audio entry points. Private originals, compiler products and comparison records remain ignored. Metal execution, copied-target/stencil bindings and native frame comparisons remain required; HLSL byte equality does not establish cross-API pixel fidelity.
