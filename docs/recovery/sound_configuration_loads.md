# Sound configuration resource lifetime

`SoundConfigurationLoads` recovers the connected-cache resource control lane of native `MSound::SoundConfig`. It owns the exact filename lookup, stable configuration objects, their original manager references, the actual serial load/completion tasks, and deferred configuration retirement. It now also owns each configuration's active/retiring archive resource owners and their borrowed voice definitions; see `sound_archive_resources.md`. Its prepared inputs require the real cache/mount/reader owners and an upstream physical resource mapping. The expected spec is explicit input from startup; native spec initialization has not been recovered.

This lane has no cue, audio-service, clock, focus, or script registration producer. The enclosing native `MSound` owner and its 50 Sound operations are still open. An actual containing owner must update its Parallel group before advancing this Serial/retirement phase. No generic serial-before-parallel wrapper is substituted.

The executable oracle is `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Static Binary Ninja observations and original byte admissions are retained in the ignored registry and producer-frontier jobs. They are executable-derived evidence, not native process captures or playback parity.

| Native behavior | Executable evidence | Authored behavior |
| --- | --- | --- |
| Suppress an exact live filename duplicate | `0x140380ed0`, raw `0x3802d0` | One configuration and one acquired manager reference; no second read |
| Own filename and prefix preceding the last slash or backslash | `0x140390e10`, raw `0x390210` | Preserve spelling; retain prefix without its final separator |
| Enqueue load immediately before completion | `0x140391133`–`0x14039114e` | Actual `SerialTaskSet` pair; pending head blocks completion |
| Connected request begins a real ResourceManager load | `0x14038c0c0`, `0x140248dc0` | Real acquired cache entry and concrete prepared reader request; no cache-hit substitute |
| Borrow completed PSB without another pin | `0x14038ace0`, `0x140248410` | Borrow the cache's actual `PublishedPsbFile`; retain its original reference |
| Set config completion before metadata diagnostics | `0x14039132e`; callback `0x140391310`, raw `0x390710` | A nonnull delivery sets the latch before id/spec/version inspection |
| Query the completion latch independently of metadata or audio success | `0x140380c50`, `0x140380cd0`, `0x140390d50` | `GetLoaded` and `GetLoading`; missing lookup returns false for both |
| Remove lookup and retain an unloading config | `0x140383ce0`, raw `0x3830e0`; `0x140391590` | Pending unload retains its serial captures and manager reference until completion |
| Retire after completion and archive/cue readiness | `0x140390d60`, `0x140381240` | Retire its config only after both task borrows and its retiring archive owners have ended |
| Cancel cache task by releasing one reference and retiring the task | `0x140393a40`, raw `0x392e40` | Balance the original reference exactly; do not report cancellation of selected I/O |

The completion id is `sound`; `sound_archive` follows the callback's unrecognized-id path. Compatible version is exactly `1.02f` after the native float conversion, without an epsilon. The original float accessor `0x1404ca7b0` (raw `0x4c9bb0`, 231 bytes, SHA-256 `5b3da03d398ea771365c7d02f71643e4adf71473e9c969acce2705fa2a1b64a8`) matches the original PE bytes and converts numeric/boolean atoms to float. Adjacent float32 versions remain old/new diagnostics. Missing or malformed fields and nonfinite versions have a separate safe diagnostic; their malformed-conversion parity remains open.

Failed or cancelled delivery never fabricates a PSB or sets a successful native completion latch. Its diagnostic is a separate authored terminal rejection that permits safe resource cleanup after the actual serial captures retire. The original callback would dereference a null delivery after writing its latch; no crash-path equivalence is claimed. On a live rejected lookup `GetLoading` still reflects the unchanged native completion byte, so callers must inspect the delivery diagnostic separately.

Private fixtures exercise independent PSB3 and raw-block MZS vectors, exact float boundaries, real asynchronous reader/cache delivery, pending unload followed by coalesced reload, last-reference eviction, failed production, cache-task cancellation before/after physical delivery, and admission rollback/capacity. Conditional ASan/UBSan validation compiles a bounded first-party source closure with the reviewed audio-id receiving change. It does not validate the published core or game fidelity. LeakSanitizer is unavailable on this macOS platform.

Next: connect the actual startup spec and enclosing `MSound` owner, recover cue owners and their retiring-source callbacks, and implement the real audio startup failure states and loop/focus lifetimes before publishing Sound operations. Continuous mix/commit, resampling/sample-loop traversal, type2 streaming, native shutdown/reset, and original-resource differential witnesses remain open.

The 371/343 checkpoint additionally validates all five fixture groups against the actual rebuilt public shared core, with the reviewed receiving prerequisite present. Containing cache, processor, serial-task and script receiving checks pass. This public component validation does not establish native audio readiness, enclosing MSound behavior or playback fidelity.
