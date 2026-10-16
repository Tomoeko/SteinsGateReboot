# Original Motion shared and per-player random ownership

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Explicit read-only Binary Ninja view `candidate:view:1:UEU`. Address suffixes are virtual addresses relative to `0x140000000`.

## Actual constructor input

The genuine plain child player constructor `506e50` calls its manager's virtual +28 at `507220`. Concrete MMotionLayer manager vtable `652d28` routes this to `52b0e0`: acquire the actual manager lock through virtual +38, lazily allocate a 16-byte shared random owner at manager +198, initialize it if absent, obtain one unit float, and release through virtual +40. This is a shared manager stream, not the script CRT rand state or a copied parent player stream. The lazy installation, manager lock provider and runtime seed source remain required external owner phases.

Shared initialization `4f98d0` reads 624 actual seed words through the runtime owner at global pointer `a96fb8`, field +18. It calls `4f9a60` with 19650218, initializes the 624-word MT array with multiplier 1812433253, then performs the observed 624-word seed mixing and 623-word final mixing, with multipliers 1664525/1566083941 and wrap copies of the last word to word zero. It finishes with state[0]=0x80000000 and index zero. Source `MotionSharedRandom` requires exactly these 624 supplied words. It supplies no entropy source or default seed and does not alias resource-cipher's four-word seeded owner.

## Exact streams and values

`4f9c80` advances one MT slot per call, combining the current high bit and next low 31 bits, distant slot 397 with its native in-place wrapping, and conditional 0x9908b0df. It tempers with right 11, the native pre-shift masks 0xff3a58ad/0xffffdf8c at left 7/15, then right 18. Indices 227/623 and wrap timing retain their original behavior. `4f9a30` converts `(word & 0x7fffff) | 0x3f800000` to float and subtracts one. It yields the actual quantized [0,1) unit sample; no division or host random library is substituted.

Actual child constructor instructions `507223–507251` multiply that rounded float sample by float 2^32, truncate through signed 64-bit conversion, then store its low 32 bits as the fourth per-player word. The first three words are 123456789/362436069/521288629. Source `MotionPlayerRandom` accepts only finite nonnegative samples in the actual 23-bit unit grid, excluding negative zero and values outside [0,1). This authored admission guard does not assert native behavior for impossible manager samples.

The independent player generator `51cae0` advances the four-word xorshift state and applies the same low 23-bit float construction. Later player draws do not consume the shared manager stream. Actual player cloning copies the four state words at `5115ea`, permitting independent continuation of the copied state. No task, listener or resource ownership is added by these state owners.

## Bounded proof and remaining owners

An unchanged-code Unicorn probe executes actual `4f98d0/4f9a60`, actual `4f9c80/4f9a30`, the original constructor's scalar seed instruction block and actual `51cae0`. Sixteen supplied runtime seed arrays include all-zero, all-one-bit, increasing and randomized words. Each run compares all 624 initialized state words, the actual manager seed sample/four child words and 768 mixed shared-word/shared-float/player-state/player-float records, crossing multiple index cycles. There are2,028,416 native instructions, sixteen bounded state-allocation service boundaries,12,288 raw shared words,12,288 shared unit floats and 12,288 four-word player state/unit-float records. Total compared output words: 96,080. Native float construction is also checked independently against each observed tempered/shared or final-player word. Fixture SHA-256: `6433bc7a6962603f09f6f2c5c51a97a00517ec071d8a14c9b54a13acb8a9805f`.

O1 ASan/UBSan direct-source execution passes every output word and asset-independent known synthetic vectors, separate-stream ownership, copied-player continuation, valid seed extremes and invalid-sample rejection. Strict warnings and formatting pass. Peak new job is744 KiB; verified objects/executable are pruned. Only the two focused first-party units are compiled/linked; no whole core/library copy/link, original game process/window/audio, binary file write or analysis mutation occurs. The emulator supplies only the runtime's prepared seed-input pointer and bounded allocation service; no process/system seed creation is claimed.

A complete selected-child constructor still requires its real exclusive player/tree owner, manager and runtime seed producer, copied root plane/transform order and motionIndependentLayerInherit state, rendering controls, source selection/reset, inherited projection/stencil/clip and publication. Kind 3 admission remains closed. This is the concrete native PRNG state/input dependency, not a placeholder Motion player, substitute random model or runtime seed invention.

Coherent public ASan/UBSan and portable Release fixtures pass at the 324/299 checkpoint, with unchanged-code native witnesses replayed through the public core. Real enclosing child/frame/resource ownership remains open.

Read-only seed-producer inspection identifies process initialization `0x1404f9ae0`: it constructs a 40-byte MSeedGenerator/MLooperVSyncHook, allocates and zeroes 2,496 seed bytes, samples the first halfword, clears rotation/index state and publishes global `0x140a96fb8`. Its 48-byte clock task is registered through `0x1404e0120` in the real looper. Exact hook timing, looper ownership and shutdown remain unintegrated.

The counter helper `0x1405534f0` calls QueryPerformanceCounter but copies only its low 16 bits. Hook `0x1404f9bb0` XORs that word with three caller-stack halfwords the helper did not write, rotates the result, and accumulates it into 1,248 seed halfwords. These incidental native bytes are unresolved inputs; the portable implementation must not manufacture zeros or read uninitialized C++ storage to reproduce them. The already recovered random owners require the actual supplied 624-word seed state.
