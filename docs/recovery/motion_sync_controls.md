# Motion sync and loop controls

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses below are virtual addresses in this executable.

The real first-dialogue `basicpicture.nut:dispSync` requires `Motion.skipToSync`; its adjacent `canDispSync` checks `getLoopTime() < 0 && getPlaying()`. Both root and actual borrowed child players now use their existing selected command owner. Child operations validate the containing task, generation and exact child before mutation.

Native `0x14051fd30` returns when stopped or loopTime is nonnegative. Otherwise it scans the cached tag array in stored order, choosing the first type1 tag at or beyond evaluation time whose actual `sync` value converts to true; absent a match it uses lastTime. The selected root is checked after the scan. The existing tick assignment writes tick/evaluation and the pending assignment bits. It does not advance a frame, dispatch events, clear sync waiting or mutate initial-boundary flag `+0x194`. This corrects the earlier open-domain attribution of that flag to skipToSync.

`0x14051b380` reads sync waiting at `+0x27c`; `0x14051dd50` clears only that byte. `0x140059880` returns raw float32 loopTime at `+0x208`. Checked captured descriptors dispatch the typed instance accessors; detached skip/release do nothing, detached sync is false and detached loopTime is positive zero. Extra arguments are ignored by these native wrappers.

The retained native witness covers58 skip/receiver/sync cases,31,145 dispatches and546 exact executable instruction sites, plus28 raw loop-time/core/captured/detached cases,1,582 dispatches and80 sites. Three skip and two loop accessor-error boundaries are explicitly prepared. Retained raw float words include signed zero, infinities and NaN payloads. These are isolated original-instruction measurements, not full native VM construction or whole-frame parity. Strict sanitizer fixtures additionally exercise actual selected resources, queued sync/action delivery, root/child aliases, independent parent ticks, stale child rejection and task retirement. Native frame and timing comparisons remain open.
