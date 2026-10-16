# Copied additive and overlay vertex color

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`; Binary Ninja view `candidate:view:1:UEU`. The direct-game run reached a copied packet with attribute bit4 absent after the genuine Wind calls; its old receiver rejected the unimplemented branch. The retained failure alone identifies blend0x110 or0x112, not a specific asset or exact mode.

The existing material producer keeps source attributes and sets bit4 only for its actual neutral/default uniform color, modulation0x808080ff and opacity255. Nondefault colors legitimately retain that bit absent. Selected frame commands preserve the material attributes; ordinary frame submission retains them as the packet key and rotates the original RRGGBBAA color to AARRGGBB. Mask-only geometry uses its existing separate key4. No flag is forced by this recovery.

Native54D8A0 tests record attributes+20 bit4 at54DAC2. If absent, blend bit0x10 selects color group8; if present, color group0 ignores vertex color. Both blend0x110 and0x112 enter family0x100 and select operation1 or3 after masking0xFF0F. RGBA texture group1 produces indices `900+(12+8)*19+operation`:1281/1283. Actual mask-resolve auxiliary group1 adds19:1300/1302. Existing skip-color1129/1131 and darken1137/1289 are unchanged. Native5316C9 selects blend10 when enabled and7 when disabled; enabled copied RGB replaces the target while retaining destination alpha.

| Program | File offset | Bytes | Original SHA-256 |
| --- | --- | ---: | --- |
|1281 additive|0x89da90|1228|5f66fb777d22a3d91b79522bcdf746fb4ef19d68ff3e77362330feb399ebf8d8|
|1283 overlay|0x89e460|1484|6568f874f59d63b622951a25345e54a5c366a65ee7e5a5e3c763b98d1c7107aa|
|1300 masked additive|0x8a3ff0|1496|bcc95a23028e08874ef953c8826c773cc1ede866b5cb6c3a1d1ef0b69852ba7a|
|1302 masked overlay|0x8a4bd0|1752|9c13000d023fb20851acdadfd7075fec088ffbee6c2939905c40996ece715890|

All four sample sourceRGBA and multiply by actual vertexRGBA, then double RGB only. Additive clamps the source-plus-copied sum to1. Overlay uses a strict per-channel copied-destination comparison below0.5, with the doubled source retained even above1. Source alpha times vertex alpha interpolates the combined RGB against the copied destination; masked programs multiply actual mask alpha afterward. Copied destination alpha is not sampled for the result. The RGBA8Unorm target clamps the final output; source color is not prematurely clamped.

The readable HLSL references reproduce all four complete original containers with the retained D3DCompiler_47/helper. Initial HLSL scalar-vector constructor failures remain retained separately; explicit three-component constructors fixed syntax without changing arithmetic. Compiler compatibility does not identify the original compiler build or establish D3D/Metal GPU equivalence.

The receiver retains its24 ordinary pipeline indices and old copied24..27, appending1281/1283 at28/29. Actual live typed preparation grows from28 to30 entries and preserves one-to-one description/output order. Physical texture/channel, copy-region, owner/resource-domain, affine, scissor, queue fingerprint, frame abort and retirement checks remain unchanged. Only the newly recovered copied color branch is admitted; non-RGBA and other unmeasured copied operations remain gated.

Source-only receiving includes1536 direct offscreen Metal pixel comparisons acrossVS1/4, blend7/10, both operations, source/vertex alpha and nonuniform copied colors; actual mask-resolve and connected packet key1/2/3 cases retain existing abort/replay checks. Expected RGB uses the original equations and existing one-byte direct/two-byte connected tolerances; alpha ownership is checked exactly in the direct fixture. GPU execution is performed by the receiving owner, with no hardware audio/window activation. No cross-API bit identity or whole-scene native pixel claim follows from this source package.
