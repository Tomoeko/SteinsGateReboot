# Raw texture color words

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

`0x1404f96b0` stores `RRGGBB00`. Each binary32 component is multiplied by 255, truncated with the original 64-bit `CVTTSS2SI`, narrowed to its low DWORD, then capped with an **unsigned** comparison against 255. Negative integral components can therefore become 255, while small negative fractions truncate to zero. Integer-indefinite conversions have a zero low DWORD. The portable helper explicitly guards the 64-bit conversion range and uses defined unsigned narrowing; it does not rely on undefined float-to-integer casts or reproduce floating-point exception flags and NaN payload propagation.

The draw path `0x1404f95f0` through `0x1404c0900` ORs the low opacity byte into the packed word, then rotates `RRGGBBAA` into the renderer's semantic `AARRGGBB`. This is an OR, including any preexisting low byte. Opacity remains separate full-DWORD state outside this helper.

Two independent original-instruction comparisons cover 1,000 arbitrary/focused color inputs each: 24,000 instructions for quantization, native result SHA-256 `1aefa6ca357d3cf56568b30bc1d3b94fba6674adcbe40115504fd855c292bee3`; 43,000 for draw packing, SHA-256 `92a566506e21e6a40ec40dc0b2501ff6baa8f57b0409c1c2f7d8c7c050243265`. Every result word matches under ASan/UBSan. Three independent fixture groups exercise ordinary truncation, unsigned/modulo/indefinite conversion and opacity/channel packing.

The draw oracle observes the boundary at `0x1404f9260`; it does not validate packet allocation, topology, GPU output, script attachment, texture restore or background ownership. Those remain separate recovery checks.
