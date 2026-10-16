# Ordinary motion quad batching recovery

Evidence date: 2026-10-12. Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses refer to the explicitly selected original PE Binary Ninja view. Original bytes and analysis metadata remain unchanged.

## Accumulation and ownership

`0x140556c20` takes the ordinary quad shortcut when both subdivision counts equal 1. Geometry and incoming colors have four row-major corners. For batched modes, UV origin/extent values multiply retained inverse texture dimensions at render owner `+0xa4/+0xa8`. It computes left/top first, then adds the separately multiplied pixel width/height for right/bottom. This preserves the original float32 operation order. Inputs must already contain evaluated positions, the effective padded texture dimensions, and any required renderer color conversion.

| Mode | Accumulated positions/UVs/colors | Accumulated indices |
| --- | --- | --- |
| Unindexed strip | Corners 0,0,2,1,3,3 | None |
| Indexed strip | Row-major corners 0,1,2,3 | Base + 0,0,2,1,3,3 |
| Unindexed list | Corners 0,1,2,2,1,3 | None |
| Indexed list | Row-major corners 0,1,2,3 | Base + 0,1,2,2,1,3 |

The duplicate strip entries remain inside accumulated buffers. The final batch helper removes only their two outer entries, preserving internal degenerate connections between quads. Indexed modes add the pre-append vertex count to each index.

A key different from render owner `+0xa0` flushes the prior batch through `0x140555490` before storing the new key and appending. `MotionQuadBatch` follows that phase order using exclusively borrowed, preallocated streams. Its count/key state stays live during submission and counts reset after success. A failed submission preserves the prior key, counts and data; the new quad is not consumed. Empty flushes skip submission. Caller storage must outlive the exclusive borrow, and submission must not mutate it. Append and flush allocate no storage.

The supplied stream capacities, a maximum of 1,048,576 entries, explicit finite-position/UV scope, and rejected recursive append/flush are authored safety restrictions. They do not claim the native dynamically growing vectors had those bounds. Insufficient capacity rejects rather than silently changing batch boundaries. Preparation validates a new quad before changing the queued batch.

## Validation and remaining work

Independent ASan/UBSan fixtures cover all four modes, internal strip connections, base-adjusted indices, exact UV values, live key/count phases, key-change flush/retry, submission failure, empty flushing, rejected recursive mutation, invalid input and insufficient prepared storage.

One thousand bounded original cases accumulated 4,529 quads with finite positions/UVs, varied inverse texture dimensions, arbitrary color words, repeated keys and key transitions. Unchanged `0x140556c20`, final flush `0x140555490`, and original preallocated point/integer append/insert helpers executed 2,959,547 instructions. Only the buffer-only backend callback was substituted; native vector allocations were not required. Every authored active position/UV/color/index word, callback packet, key and stream count matched. Trace SHA-256: `d4aef6b9ce534031a9500c9481a8e890aa20fd382fca4b1e8e359e462fada0c9`. Private vectors/report remain ignored under `Files/research/runtime/`.

General grids and polygon meshes, direct mode 0, scene buffer sizing/invalidation, render-state and clip/stencil orchestration in `0x140555610`, nonfinite arithmetic/exception flags, and GPU output remain open. This tranche is the measured ordinary-quad accumulation stage; full motion rendering acceptance still requires those behaviors.
