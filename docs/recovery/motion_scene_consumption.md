# Motion draw queue consumption

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Native analysis uses `candidate:view:1:UEU`.

Player Draw `0x140512450` lends its retained row-pointer span through `0x1402f6520`; it does not advance the player, clock, geometry or events. Manager Draw `0x1405123e0` assigns row ordinals, sorts and calls the renderer synchronously at `0x140512436`. After that call returns, `0x140512443` resets the queue end to its beginning. The actual player and manager task thunks are `0x14052afd0` and `0x14052afe0`.

The rewrite previously kept a successfully received manager queue until outer frame completion. A later draw of the same Motion layer through a transition reached the real ScenePlayer mutation guard: the player was idle but its already submitted scene remained borrowed. Failure-only attribution captured `Operating=0`, `scene_can_close=0`, one queued scene and its actual Draw stack. The recovered boundary consumes that CPU publication immediately after `DrawActualFrame` returns.

The receiver has copied vertex/index data into host-owned inputs and retired its borrowed prepared frame by that return. Its input leases and copied GPU textures remain owned until the existing outer End/Abort cleanup. Completed queue slots become null markers; their occurrence count and texture-size storage remain reserved until outer completion. This preserves capacity limits while avoiding later access to a retired CPU scene. Failed occurrences retain their actual context for existing Abort cleanup.

No ScenePlayer, SceneContext.Append, update, resource, retirement or reentrancy guard is relaxed. Appending the same scene twice before one flush remains rejected: the native pointer-alias case overwrites shared row ordinals and needs a separate representation if required by a recovered caller.

A bounded original-instruction witness passes 16 plans, 32 flush calls and 24 nonempty submissions over 198 independently checked sites / 655 bytes. The renderer sees each live queue before the original clear; sequential submissions preserve row identity, ordering, geometry and opaque player state. The separate alias-before-flush lane retains its different ordinal behavior. Preallocated storage, concrete renderer observation and bounded CRT movement are declared prepared boundaries; this is no GPU or asynchronous-retirement fidelity claim.

Four independent quiet Metal groups pass ASan/UBSan with API validation: ordinary and copied repeated draws without update, stable actual task/scene state, copied-target growth, source-first CPU retirement before End, failed-second-occurrence Abort/recovery and a genuine two-occurrence capacity limit. Source textures stay live until End. The first receiving attempt linked the old static-archive member; its failure is retained. Corrected receiving selects exactly one accepted FrameCycle object and passes the unchanged fixture.

Whole-game progression, original D3D pixels, hardware timing and broader alias domains remain separate acceptance checks.
