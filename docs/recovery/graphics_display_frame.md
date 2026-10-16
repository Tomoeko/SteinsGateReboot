# Display frame ownership

Reference: supplied STEINS.GATE.REBOOT executable, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

Native Display construction `0x1404c4540` appends its Pre clock before Post, enabled. Pre `0x1404c4a90` and Post `0x1404c4930` contain the frame/pool phases. Destruction `0x1404c2460` retires contained resources before unlinking Post and Pre. The C++ owner preserves those relationships through exact Looper registrations and held containing mutations; unrelated clock retirement vetoes cannot orphan constructor rollback.

`GraphicsDisplayFrame` owns the ordinary frame cycle and synchronization state. It borrows the real Looper, workers, target, hidden surface and texture cache. The Mac pump borrows both this Display and its exact application clock. Binding and retirement preflight both owners before publication; retirement rejects while either actual borrowing relationship remains. Present is admitted only through the clock's genuine scoped platform call after actual Display Post completion.

A direct-source ASan/UBSan and Metal check joins the real host counter/event/wait pump, exact variable Looper clock, Motion manager, selected red/blue packets, render records and hidden Present. Expected pixels, paused opaque clearing, worker/Post fencing, reentry rejection, pending-job cleanup, full-Post constructor rollback, reciprocal retirement and final zero-window/unchanged-foreground observations pass. One actual hidden drawable request, transfer and commit occurred. Observed Motion deltas were 1, 1, 2, 2 and 2; they are measurements from this authored run, not native animation timing acceptance. No original process or audio service ran.

Separate CPU checks show ResourceMeter construction may append its native empty clocks into idle actual lists during Common setup without asking unrelated siblings to retire. Active traversal and full physical capacity still reject; partial registration rolls back in native order.

The Metal clear is currently deferred until root drawing after updates; native Pre clears earlier. Depth/stereo/debug paths, copies during Update, changing resource registration during Prepared, original embedded-font/bootstrap providers, automatic Layer frame attachment, original scenes, complete model/animation support and native shader/pixel fidelity remain open. Every model and variant requires evidence; the bounded scenes are component checks.
