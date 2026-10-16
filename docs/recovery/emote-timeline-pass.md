# Emote timeline pass

Reference: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are executable virtual addresses; Binary Ninja view `candidate:view:1:UEU`.

Registration `1402B0661` captures member `1402AEF50` through the typed void adapter `14003A0D0`. Extra arguments are ignored, detached receivers do nothing, and successful calls return no Squirrel values. Live receivers call property wrapper `1402CB370` and core `1402CB560`.

Pass visits actual active order and only negative-`LoopBegin` entries. Ordinary playback reissues every remaining visible key after each saved cursor through `1402CFF90`, including instant-policy tracks that normal playback skips. Each call receives the stored name, value, absolute key time as duration, and easing. Position and cursors do not change. Ordinary entries retire unless flag4 retains them; retained ordinary entries can dispatch again on a later Pass.

Difference playback with flag2 first sets blend ratio 0 over 20 ticks, easing 0 and stop-after-fade, then sets flag4 and dispatches only remaining instant tracks. Already-passed difference playback skips both dispatch and fade restart. The portable scheduler and track owner retain the same flag state without resetting playback. Normal advancement later completes the fade.

Variable dispatch marks property dirty before route lookup, including absent names. Immediate values and selector fanout may change synchronously. The containing Plain/Common/clock publication phases do not run. This call is distinct from crossing to the final time or completing every transition immediately.

Original-instruction comparison passes 35 timeline plans and 20 wrapper cases, observing 98 variable dispatches and six fades. Independent audits match 1,328 sites and 4,892 bytes to the executable and retain zero outstanding controller allocations. Native SetVariable is an explicit operand-recording harness boundary, with separate transitive call-graph evidence; the probe does not establish final channel, full native VM/PSB or Common equivalence. Six affected strict sanitizer targets pass, including actual VM/scene owners and eight new track/scheduler groups. The optional debug recorder and unmeasured native metadata domains remain open.

A 181.56-second quiet optimized replay passes both recovered getAnimating and Pass callers, renders the later Mayuri scene and continues dialogue. It reaches only the configured 180-second observation limit, with no unsupported method or correlated title return. All 174 selected inputs remain unchanged. This bounded continuation does not establish complete routes or native pose/audio fidelity.
