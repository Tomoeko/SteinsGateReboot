# Emote active playback information

Reference `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Read-only inspection targets Binary Ninja `candidate:view:2:UEU`; addresses below are executable VAs.

Registration `0x1402B15A5` binds `getPlayingTimelineInfoList` to `0x1402AEA10`, a tail jump to the same `0x1402AE7E0` implementation used by `getPlayingMotionInfoList`. The raw-VM function ignores extra operands. Attached receivers return one array; detached receivers return zero values before accessing the containing player. Each array element is a table with only `label` (string), `flags` (integer) and `blendRatio` (float).

Count `0x1402BECB0` and indexed label `0x1402CC4D0` read the active string vector at MEmote+350/+358. Play `0x1402CBE60` normalizes its input through `0x1402D24C0` before appending a unique occurrence; replay leaves its order unchanged. This is playback order, independent of catalogue sorting. `0x1402CC49C` exports stored flags at node+50 with the parallel mask `0x1` set. Blend lookup `0x1402CA2E0` reads the published cache at node+78, rather than advancing or reading the unfinished transition target.

The portable scheduler returns owned copies from its genuine active entries. Its containing player retains the existing live/idle/queued-state admission; both script aliases build the original table fields without coercing extra arguments. No track preparation, play, clock advance, pose publication or resource registration is performed by this query. Plain Clear leaves scheduler membership intact.

Focused fixtures cover empty and unprepared catalogue entries, non-catalogue active order, fullwidth normalization, replay, flags0/1/2, pending/published blend, stop, independent result lifetime, the actual VM aliases, extra operands, plain Clear and detached receivers. All three selected scheduler, containing-player and script-binding ASan/UBSan suites pass. Static executable analysis establishes the field/control flow; a complete native VM differential capture remains open. The subsequent original-sequence sweep is recorded separately from those fixture checks.
