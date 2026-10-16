# AffineLayer projected coefficients

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

Helper `0x140054d40` projects the origin and the unit X/Y endpoints through the actual homogeneous matrix. It subtracts the projected origin from each endpoint to publish six coefficients. Perspective division, addition grouping and binary32 cancellation are preserved; copying the raw upper matrix coefficients does not recover this behavior. A large translation can collapse a unit delta through ordinary native float rounding.

The inverse arithmetic at `0x140058927`–`0x140058a1f` belongs to the real AffineLayer process. It computes `YY*XX - XY*YX`, divides each inverse coefficient separately, and sign-flips off-diagonal coefficients by XOR. An exactly zero determinant publishes identity, including zero translation. Unordered determinants enter the arithmetic branch; full nonfinite payload parity remains unverified.

The stored offset numerators specifically use `translationY*YX - translationX*YY` and `translationX*XY - translationY*XX`. These are the original field selections. Their coupling to native coordinate/picking consumers remains open, so this preparation function must not be treated as proof of a conventional inverse for arbitrary caller semantics.

A bounded original-instruction corpus covers 2,000 matrices, 400 singular fallbacks and 24,000 projected/inverse output words with zero differences across 159,600 instructions. The helper and inverse fragment execute unchanged with no substituted calls. The inverse fragment uses the native initialized constant/zero registers and prepared output pointers. Plan SHA-256: `03ec5adf5bb41917b5c89786c22385961d5041948e10a85a5589a15115b9078e`. Output SHA-256: `325e58403d5e6e806833482c5c0204e3e404b64711b42d1c101b9a3a423e2df0`.

Three independent sanitizer groups cover ordinary/projective unit deltas, original stored offsets, singular identity fallback, large-translation cancellation and signed-zero inverse terms. This scope does not recover the enclosing task's callbacks, three matrix roots, linked layer registry, angle/scale extraction, picking or GPU/frame fidelity.
