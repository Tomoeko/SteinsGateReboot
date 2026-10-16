# Emote state assignment

Reference: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. All addresses below are executable virtual addresses. Binary Ninja analysis explicitly targets `candidate:view:1:UEU`.

The original `assignState` adapter at `1402AC820` extracts a typed source wrapper from physical argument 2 before its detached-destination test; extra arguments do not change this extraction. Member `1402ACD30` skips detached sources and identical property owners. `1402BC870` copies property/controller state into existing destination owners. It does not replace the registered model, root, task, renderer, metadata calibration or variable publication dirty flag.

Noncyclic controller state crosses native export/import `1402D7180`/`1402D74A0`; angular controllers use `1402D8E10`/`1402D8FF0` and independent queued values. Coordinate assignment retains destination unit policy. The source can retire before the destination's next genuine advance.

Wind assignment copies authored fields before calling native `1402D15B0`. Existing destination simulation endpoints and bands survive even when the copied endpoint fields differ. A zero source cache stops it; a stopped source with retained nonzero cache can restart the destination. No source wind bands or shared wind RNG state are cloned.

The active variable map and eye PCG state are copied. Destination schemas, callbacks and channel identities remain local. Source named controls visit the first exact-name destination match. Graph paths are recomputed from imported endpoints against destination graph metadata. Eye parameter selection must address the destination's actual parameter vector safely; blink phase retains the destination's borrowed RNG. Selector import copies queued state without invoking variable IO. A clean zero-time update publishes copied active values without advancing controller/property phases.

Timeline assignment visits source active order and admits names present in the destination catalogue. It restores flags with bit 1 set, clears the loop marker, seeks without variable callbacks, and copies blend/stop state. Per-track difference transitions match each destination name to the first source name. The destination's previously computed additive values remain until genuine advancement.

Common physics assignment is described in [emote-physical-assignment.md](emote-physical-assignment.md). A disabled destination does no physical assignment; an enabled destination performs its actual zero-time layer/Common phases. The separate global additive-transition producer, legacy physical families and unsupported cross-metadata domains remain explicit recovery boundaries.

Private controller comparison executes unchanged original instructions for five plans and fifteen wrapper cases; the portable comparison matches 160 current/continued words exactly. Four additional native wind cases distinguish the assignment order. Named controller, timeline and full Common ownership fixtures remain separate from native numerical and whole-game acceptance. No component test proves the reported menu restart or complete game fidelity is resolved.

An optional actual-resource CPU check visits all137 archive entries and all40 Emote assets: two real containing owners per asset, one first actual main timeline when available,40 assignments and240 consumed frames. Each destination continues after source-first retirement;143 texture uploads and143 releases balance, with zero final player/UID/cache borrows. Opaque CPU texture handles exercise the existing fixture seam; GPU pixels, every timeline and native Common numerical equivalence are separate acceptance checks.
