# Native affine angle

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses below are executable virtual addresses.

The affine matrix process calls float atan2 at `0x140058a2f` with projected YX in XMM0 and XX in XMM1. The recovered dispatcher `0x14057c3f0` chooses SSE2 body `0x14057c410` when the native feature byte at `0x140a9cd98` does not select the separate FMA profile.

`PrepareAffineAngle` reconstructs finite-input results, including signed zero axes, reflected quadrants, extreme exponent shortcuts and gradual subnormal outputs. Native binary32 arguments convert exactly to binary64; ordinary ratios use the measured small-ratio polynomial or quantize to the native 241-value table at `0x1408e49e0`, representing atan(i/256) for i=16–256. The authored table is readable decimal binary64, round-tripping every original word. Reduction and multiplication/addition order match SSE2, with compiler contraction disabled. The final quadrant value converts once to binary32.

For tiny positive-X ratios, native scaled underflow handling retains the same finite result as binary64 division followed by the final conversion: the ratio of any two nonzero finite binary32 values remains representable in binary64. Differential execution verifies this replacement arithmetic for the tested normal/subnormal cases; it is not a claim about native status flags.

Unchanged original dispatcher/body execution covers 45,928 cases: every pair in a signed-axis/extreme set, both sides of table quantization boundaries in all quadrants and swapped ratios, random finite raw bit patterns, and ordinary finite geometry. All output words match over 4,480,638 original instructions. The probe prepares masked round-to-nearest MXCSR and observes 12,975 native math-status helper calls at `0x140596460`; it does not execute OS exception/status services. Plan SHA-256: `b615ea8c637fa1c056004a5b1db9afe5ce9852e6ffc7190d04075f4bb2921c93`. Output SHA-256: `4be6c5d041ac14d88b3dfd103c6650715600b389d96fcb8b3b966a748419e14d`.

Four independent ASan/UBSan groups cover signed axes, ordinary quadrants/scaled ratios, subnormal and extreme output, and explicit unsupported nonfinite rejection. This is finite-result fidelity for the SSE2/round-to-nearest profile. NaN/Infinity processing, FMA dispatch, other rounding modes, errno/fenv observations, full affine matrix callback/task integration and native game pixels remain open.
