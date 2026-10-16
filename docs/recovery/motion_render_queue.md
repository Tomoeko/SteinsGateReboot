# Motion render queue recovery

Evidence date: 2026-10-12. Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses refer to the explicitly selected original PE Binary Ninja view. Original bytes and analysis metadata remain unchanged.

## Recovered behavior

Render-manager flush `0x1405123e0` writes each queued command's 32-bit insertion ordinal at command offset `0x00`. Duplicate pointers therefore share the ordinal from their final queue position. Comparator `0x140518880` orders float `+0x28`, then float `+0x0c` only when the primary values are ordered equal, then the unsigned ordinal only when both floats are ordered equal. Signed zeros compare equal. An unordered float comparison returns false without using a later key. The coordinate meaning of these two fields is unresolved; authored fields remain `Primary` and `Secondary`.

Native sort `0x140501b00` uses insertion sorting through 32 entries, median-three partitioning, a median-of-three-medians selection above a last-to-first distance of 40, and a heap fallback when its shrinking sort budget reaches zero. Median/partition helpers are `0x140500cd0`/`0x140500e30`; heap adjustment is `0x140501070`. The explicit readable algorithm is necessary because these NaN comparisons do not satisfy the ordering required by `std::sort`. Native insertion first tests the item against the first entry and can move it across unordered intermediate entries without comparing them.

An empty queue does not call submission. A nonempty sorted queue remains live while the render-manager virtual submission runs, and its end pointer resets only after submission returns. `MotionRenderQueue` borrows command records, preallocates its pointer buffer, assigns native ordinals, preserves alias identity, and supplies the sorted queue to a caller-owned submission callback. No queue allocation occurs during append or flush. The default capacity of 65,536 and maximum of 1,048,576 commands are authored work limits, not recovered game limits. Recursive mutation during submission is rejected explicitly as an authored safety restriction. C++ callback failure preserves prepared entries rather than pretending submission succeeded.

## Validation and limits

Independent sanitizer fixtures cover primary/secondary/ordinal precedence, signed zeros, unordered keys, the discriminating front-insertion case, duplicate pointers, live submission and subsequent reset, rejected recursive mutation, callback failure, capacity limits, and sorting thresholds at 32/33/41/42/96/4,096 entries.

Unchanged original instructions executed over 1,000 private queues of 0–96 entries, including signed zeros, infinities, quiet/signaling NaNs and duplicate pointers. All authored permutations and final aliased ordinals matched. The 5,928,921-instruction trace SHA-256 is `1e177dabd8833449031ec34197ed99f5a7388206e5648a236370f308664d21db`. A separate 1,000-case original-sort corpus forces the sort budget to zero, covering the heap fallback above 32 entries and insertion below it; all results matched over 8,870,276 instructions, trace SHA-256 `b763e9e12d70521468a8b6bcc6a4642251a4d7eb3f8e49bc8fb9934f91b72e25`. Only bounded memory movement and, in the full queue run, private submission were substituted at foreign call boundaries. Reports and bounded synthetic vectors remain ignored under `Files/research/runtime/`.

Floating-point exception flags remain unverified. Virtual submission `0x140555610` still needs clipping groups, hidden/empty commands, batching, resource ownership, and actual GPU differential output. This queue is the recovered ordering/flush stage; it does not establish full motion rendering fidelity.
