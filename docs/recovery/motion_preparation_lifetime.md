# Live Motion preparation ownership

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`; read-only Binary Ninja view `candidate:view:1:UEU`.

Native `51bd50` installs current raw timeline/priority/tag atoms and invokes `50d950`. That operation first calls `50fd90`, which destroys each selected node's two decoded0x120 frames through `5084a0`; `514460` releases prior per-slot curve handles before reuse. These are live selected owners. A runtime-wide cache of every historical selection is not native ownership.

The authored runtime now uses immutable preparation leases and weak cache entries, retaining its safety bound of256 simultaneous owners. Root selections, real child players, owning JOIN capsules and pending raw-string event queues keep their exact preparation alive. Event prepend transfers the same leases; detached delivery releases them after callbacks finish. Registration/source backing remains borrowed, with exact live borrow accounting. External Prepare callers must retain the returned lease through every frame/source reference. Runtime Close requires all leases and players to retire.

Focused sanitizer fixtures pass300 successive generic selections,300 legacy selections,300 current tag selections, exact256 live-owner exhaustion/reuse, and child-event transfer/delivery. Both actual Metal receiver groups pass after their direct caller retains its lease. These checks establish authored lifetime behavior; complete native long-session timing, memory and game fidelity remain open. Native variable tracks are separate retained player state and must not be lost with selected-tree Clear.
