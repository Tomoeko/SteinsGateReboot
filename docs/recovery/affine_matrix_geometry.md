# Affine matrix projections

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

The portable geometry functions recover original identity testing (`0x140054f50`), partial perspective initialization (`0x140054f40`), integer translation (`0x140056a00`), homogeneous point X/Y (`0x140056880`, `0x1400568d0`) and four-corner area X/Y (`0x1400564c0`, `0x1400566a0`). Identity uses numeric comparison, accepting either zero sign and rejecting NaN. Perspective initialization writes only fourth-row Z/W and retains every other coefficient. Integer translation truncates the displacement before adding the old translation column; invalid native `CVTTSS2SI` conversions produce the signed 32-bit minimum without undefined C++ casts.

Point projections divide by the fourth-row dot product. Area results retain the original order: origin, width-only, height-only, combined corner. They do not calculate sorted bounds or an inverse. Native operation grouping is preserved with contraction disabled.

An unchanged original-instruction corpus covers 7,000 cases and 43,000 output words with zero differences across 446,374 instructions. Plan SHA-256: `d8320f5f0ba6067e07628764d4fa6118631760fee2638c2f5eeb878816539bff`. Output SHA-256: `eb4a68b07a447e7b3de845fe391f0248efc1173544a5a1996b559ddb6e68fd88`. Area output Array/ObjectInfo construction and VM pushes are observed boundaries; their arithmetic remains original. Three independent fixture groups cover partial writes, signed zero, invalid conversion, fractional translation, homogeneous coordinates and unsorted corner order.

This scope does not establish script adapters, AffineLayer task/link ownership, inverse picking, full nonfinite floating-point payload parity or GPU/frame fidelity. Those remain separate acceptance checks.
