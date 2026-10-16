# Ordinary Motion priority frame owner

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Read-only analysis explicitly selected Binary Ninja native `candidate:view:1:UEU` / legacy `view:1`.

`MotionPriorityCursor` supplies the actual stateful priority part of ordinary frame preparation. It borrows an immutable existing `PreparedMotionTimeline`; all orders remain owned by that preparation. Seek/forward/reverse allocate nothing. It does not supply variable or layer frame preparation, layer actions, hierarchy composition, packet field production, task ownership or a Motion script class.

## Native transitions

The player stores current index at `+0x210`, current/following times at `+0x214/+0x218`, count at `+0x21c`, stored indices at `+0x220/+0x228/+0x230`, and the PSB track atom at `+0x238`. Ordinary selection `0x14051bd50` retains the track and count. Constructor `0x140507300` clears the stored-index vector; its scalar cursor fields are not proven initialized and are not read by this implementation until initial seek. The portable state explicitly initializes them to zero with `Initialized=false`, reports no draw order, and rejects forward/reverse until seek.

- Seek `0x140511640` scans from the beginning. First exact time wins, including duplicated times; first greater time selects the previous index. The result clamps to `count-2`, then loads adjacent times and emits that frame's entire content with `0x140515c80`.
- Forward `0x140518160` advances while `index<count-2` and evaluation is at least the following boundary. Each transition loads the next boundary and replaces the stored content. Equal times can advance through multiple frames.
- Reverse `0x14050a6b0` advances backward only while evaluation is strictly less than current time. Equality retains the current frame, so direction and prior state matter at duplicate times.
- Emission `0x140515c80` and inline forward/reverse phases reset vector end to begin and append full decoded int32 values through `0x1402f6190`. The frame type does not gate this native emission. Final marker content is never emitted.
- Publication `0x14050ad30` reads stored indices in reverse and adds one to select its child layer, skipping root index zero. Existing prepared orders already have this reversal; `DrawOrder()` borrows their current order directly.

The ordinary clock bounds evaluation to `[0,lastTime]`. This cursor accepts finite sorted tracks with zero origin, at least two frames and terminal time at or beyond lastTime. Exact terminal equality is valid, because seek clamps to the penultimate frame and forward never activates the marker. Empty/one-frame priority tracks, a terminal before lastTime and evaluation outside that bounded domain remain unsupported. Repeated layer indices remain intact. Existing timeline preparation owns index-range and layer checks; cursor fixtures deliberately isolate full int32 emission without claiming those arbitrary fixture indices identify a real layer.

## Independent evidence and validation

The retained native fixture in the separate `track_domain` tranche executes real seek/forward/reverse, priority emission, preallocated native int32 vector append and original PSB accessors. Only bounded memory/string/cookie services and outgoing JOIN snapshot cleanup are observed. It covers 256 tracks / 4,096 transitions, including 2..7 frames, duplicate times, exact terminal seeks, zero-duration nonlooping tracks, final null content and full int32 contents. There are zero independent differences over 4,085,535 original instructions. Fixture SHA-256: `894eae9a9200fe2905e7acb47380d155e81466496ba658659167df5f34cbe224`.

The focused C++ source and test compile with strict C++20 warnings. A standalone AddressSanitizer/UndefinedBehaviorSanitizer field proof links only this new source and test, without a core library or original process. It replays all 4,096 native transitions, comparing cursor index, float32 adjacent-time bits and reversed publication order. Focused assertions distinguish duplicate-time seek/forward/reverse, exact terminal selection, initially empty output and unsupported domains. The temporary proof executable is pruned after verification; coherent public replay also matches all 4,096 transitions, and integrated sanitizer/portable Release fixtures pass in the 274/249 checkpoint.
