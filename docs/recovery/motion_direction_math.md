# Original Motion direction atan2 output

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Explicit read-only Binary Ninja target `candidate:view:1:UEU`. Address suffixes are virtual addresses relative to `0x140000000`.

The actual Motion child direction producer calls float atan2 dispatcher `57c3f0`. Its scalar/SSE branch is `57c410` (`[57c410,57c80a)`), while CPU control byte `a9cd98` can select a different kernel. This tranche recovers the original initial scalar/SSE numeric output for finite float32 inputs. CPU-selected alternate kernels, nonfinite behavior and CRT floating/error-status side effects remain unverified.

The routine converts each float input exactly to double, inspects signs and exponent differences, and handles axes/signed zero and extreme ratios before ordinary reduction. Vertical/horizontal exponent thresholds are +26, −13, −26, −126 and −150. Tiny ratios below the native lower bound return signed zero; intermediate tiny ratios use double division or the original power-of-two-scaled conversion. These branches preserve the native float output without relying on host atan2.

Ordinary reduction compares absolute inputs, swaps them when necessary and evaluates a ratio at most one. Ratios above 1/16 round `ratio*256+0.5` to a lookup ordinal16..256. The native table at `8e49e0` contains 241 double entries (1,928 bytes). A small residual plus the table entry is corrected with the native cubic coefficient before quadrant/sign restoration and float rounding. Smaller ratios use the native double polynomial and threshold at `652b58`. All coefficient/table bytes retain read-only VA/hash provenance in the private data receipt; the authored table has readable individual constants and names. Source reconstructs operations and their order, not decompiler text or a downloaded math implementation.

The resulting primitive supplies radians only. Actual child angles subsequently multiply the rounded float radians by float360, divide by native float tau `0x40c90fdb`, add the selected effective direction offset and wrap. That producer, child initial-composition state, selected coordinate derivative, real map target pose and inherited owners remain separate coupling work. No kind3 admission, task, source publication or resource/event pin is supplied by this math primitive.

## Measured validation

A bounded unchanged-code Unicorn probe executes actual `57c3f0` with the original initial CPU byte and actual `57c410` output. It records 8,192 finite float32 pairs covering signed axes/zeros, subnormals, maximum magnitudes, exponent thresholds, lookup boundaries, all quadrants and random finite words. There are 666,843 native instructions and 4,067 observed CRT-status service boundaries; no numeric result is replaced at those boundaries. A separate host double atan2 rounded to float has zero output differences over these vectors. This measurement is a bounded oracle, not a substitute for reconstructing the actual routine or a claim about every runtime CPU/floating environment.

O1 ASan/UBSan standalone direct-source execution passes independent signed-axis/quadrant/failure defaults and all 8,192 exact native output-word comparisons. Strict C++20 warnings and repository formatting pass. No core/library copy/link, original process, Binary Ninja annotation or original-installation write occurred. Peak proof job was 368 KiB; verified objects and executable were pruned.

Coherent public ASan/UBSan and portable Release fixtures pass at the 317/292 checkpoint, with retained native witnesses replayed through the public core. Full engine/cache/frame acceptance remains open.
