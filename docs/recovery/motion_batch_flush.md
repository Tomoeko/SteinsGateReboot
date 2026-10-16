# Motion batch flush recovery

Evidence date: 2026-10-12. Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses refer to the explicitly selected original PE Binary Ninja view. Original bytes and analysis metadata remain unchanged.

## Submission and reset decisions

`0x140555490` checks the position stream's begin/end at owner `+0x40/+0x48` first. Empty positions return immediately, preserving the UV (`+0x58/+0x60`), color (`+0x70/+0x78`) and index (`+0x88/+0x90`) ends. Nonempty positions use the mode at `+0x3c` and emit key `+0xa0` unchanged through virtual callback `+0xc8`. The concrete vtable at `0x140652d80` resolves that callback to `0x140559dd0`.

| Native mode | Submission | Outer entries removed |
| --- | --- | --- |
| 1 | Unindexed triangle strip | First/last positions, UVs and colors |
| 2 | Indexed triangle strip | First/last indices only |
| 3 | Unindexed triangle list | None |
| 4 | Indexed triangle list | None |

`0x140559dd0` selects primitive code 0 for the strip modes and 1 for the list modes, packing the supplied XY, color and UV streams into the existing sprite vertex bridge. Unindexed submissions pass a zero index count and null index pointer. Indexed submissions retain the selected pointer even for zero indices. Two outer strip entries therefore still produce a submission, with zero selected vertices or indices; this differs from an originally empty position stream.

All original staging ends remain live during the callback. Once it returns, the function resets all four ends to their begins, including surplus UV/color entries or staged indices unused by an unindexed submission. `PrepareMotionBatchFlush` expresses these portable decisions as borrowed submission views and `RetainedCounts` to apply after a successful submission. It does not clear the owner's buffers early or perform a GPU call. The scene owner must keep all storage alive until submission completes and must preserve the existing counts if submission fails. No allocation occurs in preparation.

Parallel-stream bounds, two outer strip entries and an authored maximum of 1,048,576 entries per stream are checked explicitly. This maximum is not a recovered game capacity. Unsupported nonempty modes reject rather than becoming an invented rendering path. Mode 0's separate direct-draw path remains outside this helper.

## Validation and remaining work

Independent ASan/UBSan fixtures cover all four slice selections, surplus parallel streams, preserved keys/pointers, empty-position preservation, two-entry zero-count strips, short-strip rejection and missing parallel streams. One thousand private supported-mode cases executed unchanged `0x140555490` and captured callback arguments, live stream counts and post-return counts through a private buffer-only callback. All authored decisions matched over 48,990 original instructions, trace SHA-256 `bdabcbae6dad2003b918af8d04a138cf0d39c61f5183cb9242f468e108f2077b`. The private vectors/report remain ignored under `Files/research/runtime/`.

Geometry/color/UV accumulation and internal strip connections in `0x140556c20`, render-state and clip/stencil orchestration in `0x140555610`, actual backend submission of zero-count packets, and GPU differential output remain open. This helper recovers the measured final batch selection/reset decisions, not complete motion rendering.
